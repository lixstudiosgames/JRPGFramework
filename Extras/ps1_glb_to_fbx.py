"""
ps1_glb_to_fbx.py — converte um .glb de modelo PS1 do legend-of-legaia-re num FBX com
esqueleto, pronto para o Unreal (SkeletalMesh + uma AnimSequence por clip).

Roda DENTRO do Blender (5.2+), sem interface:

    blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- \
        <entrada.glb> <saida.fbx> <Nome> [--cm-per-unit X] [--clips Idle,Walk]

Serve para os monstros (`asset monster-archive --glb`) e para a forma de batalha da party (o
"Download .glb with all battle animations" da página Arts do site do RE).

O que faz, e por quê:
  1. Os exportadores do RE soltam cada canto de triângulo como um vértice próprio (UV e cor
     são por canto no TMD do PS1). Solda os vértices de cada peça por posição — as posições
     são inteiras no PS1, então a solda é exata e cada peça vira uma malha fechada; UV e cor
     continuam por canto (o Blender guarda por loop).
  2. O modelo é feito de peças rígidas, sem skin (o Unreal importaria StaticMesh). Cria uma
     armature com um bone por peça e põe cada peça 100% no seu bone.
  3. A armature fica com transform IDENTIDADE (bones montados no espaço do mundo). Com a
     rotação de 180° do nó raiz do glTF na armature, o Unreal aplicava a rotação na malha e
     não nas animações: clips de cabeça para baixo e na escala errada.
  4. Tempo: os monstros vêm a 14 fps nominais (o jogo avança um quadro por passo de ação, não
     a relógio) e cada quadro do PS1 vira 2 quadros a 30 fps. A party já vem em múltiplos de
     1/30 s (Idle a 7.5, Walk a 15) e entra direto a 30 fps. O clip é assado em quadros
     inteiros a 30 fps: sem o aviso "not compatible with import frame rate" do Unreal.
  5. Loops: nos clips de ciclo (Idle, Walk, Move) o último quadro do disco é o ANTERIOR ao
     primeiro — o jogo volta ao quadro 0 sozinho. Como clip do Unreal isso dá um pulo na
     volta; o script fecha o ciclo com um quadro igual ao primeiro, um passo depois do último.
     Clips que já vêm fechados (o Idle do Gimard) ficam como estão.
  6. Escala: cm_por_unidade (unidade do PS1 -> cm no Unreal), aplicada nos DADOS (malha, bones
     e trilhas de posição), com a cena do Blender em CENTÍMETROS (scale_length 0.01). Qualquer
     escala que sobre no nó da armature (o global_scale do FBX, ou o x100 que aparece quando
     a cena está em metros) o Unreal aplica na malha e não nas animações: os clips saem
     maiores que a malha, ou tão menores que ela some.

Saída: <saida.fbx> (armature "Armature", malha <Nome>, clips AS_<Nome>_<Clip>) e
T_<Nome>.png ao lado (o atlas de textura — o FBX não carrega a textura embutida do glb).

Conteúdo extraído do disco é da Sony: entradas e saídas ficam FORA do repositório.
"""
import argparse
import json
import math
import os
import re
import struct
import sys

import bpy

# PS1 -> cm. Calibrado no Unreal com a party de batalha (Vahn/Noa/Gala) e o Gimard lado a lado,
# conferido no olho contra o DuckStation: o Vahn de batalha (~422 unidades) fica com ~183 cm.
# Monstros e party usam o MESMO fator — no disco eles estão na mesma unidade.
DEFAULT_CM_PER_UNIT = 0.433005
MONSTER_KEY_FPS = 14     # o que o exportador de monstros grava (monster_gltf.rs ANIM_FPS)
TARGET_FPS = 30
WELD_DISTANCE = 0.01
LOOP_CLIPS = "Idle,Walk,Move"   # nome antes de " / " (o "Walk / Approach" da party é Walk)


def log(msg):
    print("[ps1_glb_to_fbx] " + msg)


def clip_suffix(name):
    """'Attack #1' -> 'Attack1', 'Knocked Down' -> 'KnockedDown', 'Action 0x23' -> 'Action_0x23',
    'Walk / Approach' -> 'Walk'."""
    s = name.split(" / ")[0].replace("#", "")
    s = re.sub(r"Action\s+0x", "Action_0x", s)
    return re.sub(r"[^A-Za-z0-9_]", "", s)


def part_index(obj):
    """Índice da peça pelo nome do slot da animação ('OBobject_7') ou do objeto."""
    ad = obj.animation_data
    src = ad.action_slot.identifier if ad and ad.action_slot else obj.name
    m = re.search(r"object_(\d+)", src)
    return int(m.group(1)) if m else None


def import_fps(glb):
    """fps em que importar o glb para cada chave cair num quadro inteiro.

    Monstros (chaves a cada 1/14 s): 28 — a chave k vai para o quadro 2k, e o clip sai a 30.
    Party (chaves em múltiplos de 1/30 s): 30, direto.
    """
    with open(glb, "rb") as f:
        data = f.read()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    binary = data[20 + jlen + 8:]
    smallest = None
    for anim in gltf.get("animations", []):
        acc = gltf["accessors"][anim["samplers"][0]["input"]]
        view = gltf["bufferViews"][acc["bufferView"]]
        off = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        times = struct.unpack_from("<%df" % acc["count"], binary, off)
        if len(times) > 1:
            dt = times[1] - times[0]
            smallest = dt if smallest is None else min(smallest, dt)
    if smallest is not None and abs(smallest - 1.0 / MONSTER_KEY_FPS) < 1e-4:
        return MONSTER_KEY_FPS * 2
    return TARGET_FPS


def channelbag(action, slot):
    """FCurves de um slot numa action em camadas (Blender 4.4+)."""
    for layer in action.layers:
        for strip in layer.strips:
            bag = strip.channelbag(slot)
            if bag:
                return bag
    return None


def close_loop(action, slots):
    """Acrescenta ao fim de um clip de ciclo um quadro igual ao primeiro, um passo depois do
    último. Não mexe se o clip já fecha (último == primeiro). Devolve True se fechou."""
    bags = [b for b in (channelbag(action, s) for s in slots) if b]
    curves = [fc for b in bags for fc in b.fcurves if len(fc.keyframe_points) >= 2]
    if not curves:
        return False

    def first(fc):
        return fc.keyframe_points[0].co.y

    def last(fc):
        return fc.keyframe_points[-1].co.y

    if all(abs(first(fc) - last(fc)) < 1e-3 for fc in curves):
        return False
    kp = curves[0].keyframe_points
    step = kp[-1].co.x - kp[-2].co.x
    end = max(fc.keyframe_points[-1].co.x for fc in curves) + step
    for bag in bags:
        by_path = {}
        for fc in bag.fcurves:
            by_path.setdefault(fc.data_path, {})[fc.array_index] = fc
        for path, comps in by_path.items():
            sign = 1.0
            if path == "rotation_quaternion" and len(comps) == 4:
                # q e -q são a mesma rotação: fecha pelo lado mais curto.
                dot = sum(first(comps[i]) * last(comps[i]) for i in range(4))
                sign = -1.0 if dot < 0 else 1.0
            for fc in comps.values():
                if len(fc.keyframe_points) < 2:
                    continue
                k = fc.keyframe_points.insert(end, first(fc) * sign)
                k.interpolation = 'LINEAR'
    return True


def weld(mesh):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    before = len(bm.verts)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=WELD_DISTANCE)
    bm.to_mesh(mesh)
    bm.free()
    return before, len(mesh.vertices)


def main(argv):
    ap = argparse.ArgumentParser(prog="ps1_glb_to_fbx")
    ap.add_argument("glb")
    ap.add_argument("fbx")
    ap.add_argument("name")
    ap.add_argument("--cm-per-unit", type=float, default=DEFAULT_CM_PER_UNIT)
    ap.add_argument("--clips", default="", help="só estes clips (nome antes de ' / '), ex.: Idle,Walk")
    ap.add_argument("--loop-clips", default=LOOP_CLIPS, help="clips de ciclo a fechar no fim")
    args = ap.parse_args(argv)
    glb, out_fbx, name, cm_per_unit = args.glb, args.fbx, args.name, args.cm_per_unit
    wanted = {c.strip().lower() for c in args.clips.split(",") if c.strip()}

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    # O importador do glTF põe a chave t (s) no quadro t * fps.
    scn.render.fps = import_fps(glb)
    scn.render.fps_base = 1.0
    bpy.ops.import_scene.gltf(filepath=glb)

    parts = {}
    for o in scn.objects:
        if o.type == 'MESH':
            n = part_index(o)
            if n is None:
                raise RuntimeError("peça sem índice: " + o.name)
            parts[n] = o
    all_clips = list(bpy.data.actions)
    clips = [a for a in all_clips if not wanted or a.name.split(" / ")[0].lower() in wanted]
    if not clips:
        raise RuntimeError("nenhum clip bate com --clips %s" % args.clips)
    log("%s: %d peças, %d de %d clips, importado a %d fps"
        % (name, len(parts), len(clips), len(all_clips), scn.render.fps))

    for o in parts.values():
        o.animation_data.use_nla = False

    loop_names = {c.strip().lower() for c in args.loop_clips.split(",") if c.strip()}
    all_slots = [o.animation_data.action_slot.identifier for o in parts.values()]
    for a in clips:
        if a.name.split(" / ")[0].lower() in loop_names:
            slots = [s for s in a.slots if s.identifier in all_slots]
            if close_loop(a, slots):
                log("  ciclo fechado: %s" % a.name)

    # Pose de descanso = quadro 0 do primeiro clip (o exportador usa o idle como base).
    idle = next((a for a in clips if a.name.lower() == "idle"), clips[0])
    slot_of = {n: o.animation_data.action_slot.identifier for n, o in parts.items()}
    for n, o in parts.items():
        o.animation_data.action = idle
        o.animation_data.action_slot = idle.slots[slot_of[n]]

    # Cena em centímetros (1 unidade do Blender = 1 cm, o mesmo do Unreal) e a escala no nó raiz
    # do glb: daqui em diante o matrix_world das peças já está em cm, e malha, bones e poses
    # saem no tamanho final, sem escala em nó nenhum do FBX.
    scn.unit_settings.system = 'METRIC'
    scn.unit_settings.scale_length = 0.01
    unit = cm_per_unit
    for o in scn.objects:
        if o.type == 'EMPTY' and o.parent is None:
            o.scale = (unit, unit, unit)
    scn.frame_set(0)
    bpy.context.view_layer.update()

    welded = [weld(o.data) for o in parts.values()]
    log("solda: %d -> %d vértices" % (sum(b for b, _ in welded), sum(a for _, a in welded)))

    # --- armature com transform identidade ---
    arm_data = bpy.data.armatures.new("Armature")
    arm = bpy.data.objects.new("Armature", arm_data)
    scn.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    root = arm_data.edit_bones.new("root")
    root.head = (0, 0, 0)
    root.tail = (0, 0, 40 * unit)
    for n, o in sorted(parts.items()):
        eb = arm_data.edit_bones.new("part_%02d" % n)
        eb.head = (0, 0, 0)
        eb.tail = (0, 25 * unit, 0)
        eb.matrix = o.matrix_world.normalized()
        eb.parent = root
    bpy.ops.object.mode_set(mode='OBJECT')

    # --- malha única, já no espaço do mundo (= espaço da armature) ---
    copies = []
    for n, o in sorted(parts.items()):
        me = o.data.copy()
        me.transform(o.matrix_world)
        c = bpy.data.objects.new("tmp_%02d" % n, me)
        scn.collection.objects.link(c)
        vg = c.vertex_groups.new(name="part_%02d" % n)
        vg.add(range(len(me.vertices)), 1.0, 'REPLACE')
        copies.append(c)
    for o in scn.objects:
        o.select_set(False)
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    mesh = copies[0]
    mesh.name = name
    mesh.data.name = name
    mesh.parent = arm
    mod = mesh.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm

    # --- clips: copia posição e rotação de cada peça para o seu bone e assa ---
    # (Copy Transforms levaria junto a escala do nó raiz e escalaria a malha de novo.)
    for n, o in parts.items():
        pb = arm.pose.bones["part_%02d" % n]
        for kind in ('COPY_LOCATION', 'COPY_ROTATION'):
            c = pb.constraints.new(kind)
            c.target = o
    for o in scn.objects:
        o.select_set(False)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='POSE')
    for pb in arm.pose.bones:
        pb.select = True
    arm.animation_data_create()
    baked = []
    for src in clips:
        for n, o in parts.items():
            o.animation_data.action = src
            o.animation_data.action_slot = src.slots[slot_of[n]]
        last = int(math.ceil(src.frame_range[1] - 1e-3))
        arm.animation_data.action = None
        bpy.ops.nla.bake(frame_start=0, frame_end=last, only_selected=True, visual_keying=True,
                         clear_constraints=False, use_current_action=False, bake_types={'POSE'})
        act = arm.animation_data.action
        act.name = "AS_%s_%s" % (name, clip_suffix(src.name))
        act.use_fake_user = True
        baked.append((act.name, last))
    for pb in arm.pose.bones:
        for c in list(pb.constraints):
            pb.constraints.remove(c)
    bpy.ops.object.mode_set(mode='OBJECT')
    arm.animation_data.action = bpy.data.actions.get("AS_%s_Idle" % name) or bpy.data.actions[baked[0][0]]

    # As peças originais e as ações do glb saem antes de exportar.
    for o in list(parts.values()):
        bpy.data.objects.remove(o, do_unlink=True)
    for o in [o for o in scn.objects if o.type == 'EMPTY']:
        bpy.data.objects.remove(o, do_unlink=True)
    for a in all_clips:
        bpy.data.actions.remove(a)
    for clip_name, last in baked:
        log("  %-28s %3d quadros a %d fps" % (clip_name, last, TARGET_FPS))

    # --- textura ao lado do FBX ---
    imgs = [i for i in bpy.data.images if i.size[0] > 0]
    if imgs:
        tex = os.path.join(os.path.dirname(out_fbx), "T_%s.png" % name)
        imgs[0].filepath_raw = tex
        imgs[0].file_format = 'PNG'
        imgs[0].save()
        log("textura: " + tex)

    scn.render.fps = TARGET_FPS
    for o in scn.objects:
        o.select_set(o in (arm, mesh))
    bpy.ops.export_scene.fbx(
        filepath=out_fbx, use_selection=True, object_types={'ARMATURE', 'MESH'},
        global_scale=1.0, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
        add_leaf_bones=False, use_armature_deform_only=False, mesh_smooth_type='OFF',
        use_mesh_modifiers=False, bake_anim=True, bake_anim_use_all_actions=True,
        bake_anim_use_nla_strips=False, bake_anim_force_startend_keying=True,
        bake_anim_simplify_factor=0.0, path_mode='COPY', embed_textures=False)
    log("FBX: %s (%.5f cm por unidade)" % (out_fbx, cm_per_unit))


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:])
