"""
ps1_glb_to_fbx.py — converte um .glb de modelo PS1 do legend-of-legaia-re num FBX com
esqueleto, pronto para o Unreal (SkeletalMesh + uma AnimSequence por clip).

Roda DENTRO do Blender (5.2+), sem interface:

    blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- \
        <entrada.glb> <saida.fbx> <Nome> [--cm-per-unit X] [--clips Idle,Walk] [--block <bloco>]

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

  7. Clip morphs (com --block <bloco do monstro>, de `asset monster-archive --dump-block`): o
     jogo soma deltas por vértice na malha enquanto certos clips tocam, com um peso em trapézio
     (k1..k4) — é o que dá vida ao slime, que é um objeto só (RE: monster-animation.md, "Clip
     morphs"). O .glb do RE não os traz. Cada lane vira uma shape key Morph_NN; o peso dela é
     uma propriedade Morph_NN da armature, animada em cada clip e ligada à shape key por driver
     — o exportador do FBX amostra as shape keys a cada quadro, então cada take sai com as suas
     curvas. No Unreal: Morph Targets na malha e uma curva Morph_NN em cada AnimSequence.
  8. Semi-transparência (também com --block): o PS1 desenha certos primitivos semi-transparentes
     (ABE no mode do grupo; ABR 0 = média, 1 = soma). O .glb junta tudo num material opaco; aqui
     cada face vai para o slot PS1_Opaque / PS1_Translucent / PS1_Additive do seu primitivo.

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
    'Action 0x13 #1' -> 'Action_0x13_1', 'Walk / Approach' -> 'Walk'."""
    s = re.sub(r"(Action\s+0x[0-9A-Fa-f]+)\s+#", r"\1_", name.split(" / ")[0]).replace("#", "")
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


def clip_morph_weight(keys, phase):
    """Peso 0..1 de uma lane no cursor do clip (phase = quadros do PS1 em 12.4). O trapézio do
    FUN_800495C8 (RE: monster_archive::clip_morph_weight): sobe em k1..k2, segura até k3, desce
    até k4."""
    k1, k2, k3, k4 = keys
    f = phase >> 4
    if k1 <= f < k2:
        w = ((phase - (k1 << 4)) << 12) // ((k2 - k1) << 4)
    elif k2 <= f <= k3:
        w = 0x1000
    elif k3 < f <= k4:
        w = (((k4 << 4) - phase) << 12) // ((k4 - k3) << 4)
    else:
        w = 0
    return w / 4096.0


def gltf_to_blender(v):
    """O importador do glTF troca os eixos (Y-up -> Z-up): (x, y, z) vira (x, -z, y)."""
    return (v[0], -v[2], v[1])


def read_clip_morphs(block_path):
    """Os clip morphs do bloco do monstro (asset monster-archive --dump-block): deltas por vértice
    que o jogo soma na malha enquanto o clip toca (RE: monster-animation.md, "Clip morphs").

    Devolve (vertices, lanes_by_label, morphs):
      vertices[obj]        posições (x, y, z) do TMD de cada objeto, para achar o vértice pela posição
      lanes_by_label       rótulo do clip (o mesmo do .glb) -> índices das lanes (+0x04, +0x08)
      morphs[índice]       (keys k1..k4, [(objeto, primeiro vértice, [(dx, dy, dz)...])])
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ps1_monster_sounds import action_entries, action_labels

    block = open(block_path, "rb").read()
    tmd = struct.unpack_from("<I", block, 4)[0]
    vertices = []
    for i in range(struct.unpack_from("<I", block, tmd + 8)[0]):
        vert_top, n_vert = struct.unpack_from("<II", block, tmd + 12 + i * 0x1C)
        base = tmd + 12 + vert_top
        vertices.append([struct.unpack_from("<hhh", block, base + 8 * k) for k in range(n_vert)])

    count = block[0x4A]
    entries = action_entries(block)
    lanes_by_label, morphs = {}, {}
    for entry, label in zip(entries, action_labels(entries)):
        lanes = [ix for ix in struct.unpack_from("<II", entry, 4) if 0 < ix < 0x100]
        lanes_by_label[label] = lanes
        for ix in lanes:
            if ix in morphs:
                continue
            off = struct.unpack_from("<I", block, (ix + count + 0x12) * 4)[0]
            keys = tuple(block[off + 1:off + 5])
            records, at = [], off + 12
            for _ in range(struct.unpack_from("<I", block, off + 8)[0]):
                obj, first, n = struct.unpack_from("<III", block, at)
                deltas = [struct.unpack_from("<hhh", block, at + 12 + 8 * k) for k in range(n)]
                records.append((obj, first, deltas))
                at += 12 + 8 * n
            morphs[ix] = (keys, records)
    return vertices, lanes_by_label, morphs


# Descritor dos grupos de primitivos (RE: crates/tmd descriptor.rs TABLE): (byte3, byte4) por
# linha; dá onde ficam os índices de vértice. Bloco de textura (tsb em +6) por forma texturizada.
PRIM_TABLE = [(0x05, 0x07), (0x07, 0x06), (0x00, 0x02), (0x02, 0x06), (0x01, 0x07), (0x03, 0x0B)]
TEXTURE_BLOCK = {0x10: 4, 0x11: 4, 0x12: 12, 0x13: 16}     # flags >> 1: FT3 FT4 GT3 GT4
BLEND_SLOTS = ("PS1_Opaque", "PS1_Translucent", "PS1_Additive")


def read_face_blends(block_path):
    """Semi-transparência por primitivo, como o jogo desenha (RE: tmd.md, battle-actor-rendering.md):
    o bit 1 do mode do grupo é o ABE, e o ABR vem do tsb do bloco de textura (bits 5-6).
    No disco só há ABR 0 (média: 0,5 fundo + 0,5 monstro) e ABR 1 (soma).

    Devolve {objeto: {frozenset(3 índices do TMD): índice em BLEND_SLOTS}} só dos primitivos
    semi-transparentes (o resto é opaco). Um quad entra com todos os seus trios: o .glb o divide
    em dois triângulos.
    """
    block = open(block_path, "rb").read()
    tmd = struct.unpack_from("<I", block, 4)[0]
    base = tmd + 12
    out = {}
    for obj in range(struct.unpack_from("<I", block, tmd + 8)[0]):
        prim_top, n_prim = struct.unpack_from("<II", block, base + obj * 0x1C + 16)
        pos, seen = base + prim_top, 0
        while seen < n_prim and pos + 8 <= len(block):
            count, flags, _olen, ilen, _flag, mode = struct.unpack_from("<HHBBBB", block, pos)
            if (count == 0 and flags == 0) or ilen == 0:
                break
            f = flags >> 1
            tex = TEXTURE_BLOCK.get(f)
            if mode & 2 and tex is not None and 8 <= f <= 0x13:
                byte3, byte4 = PRIM_TABLE[(f - 8) >> 1]
                quad = f & 1
                at = (byte4 if byte3 == 0 else 8 if byte3 in (1, 2) else 0xE if byte3 == 3 else 6) if quad else byte4
                n = 4 if quad else 3
                for i in range(count):
                    prim = pos + 8 + i * ilen * 4
                    verts = [v // 8 for v in struct.unpack_from("<%dH" % n, block, prim + at * 2)]
                    abr = (struct.unpack_from("<H", block, prim + tex + 6)[0] >> 5) & 3
                    slot = 2 if abr in (1, 3) else 1
                    trios = [frozenset(verts[:k] + verts[k + 1:]) for k in range(4)] if quad else [frozenset(verts)]
                    for trio in trios:
                        out.setdefault(obj, {})[trio] = slot
            seen += count
            pos += 8 + (count + 1) * ilen * 4
    return out


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
    ap.add_argument("--block", default="", help="bloco do monstro (asset monster-archive --dump-block): "
                    "traz os clip morphs como shape keys + curvas (o slime)")
    args = ap.parse_args(argv)
    # Absolutos: o Blender resolve caminho relativo de imagem a partir de outro lugar (C:\)
    glb, out_fbx = os.path.abspath(args.glb), os.path.abspath(args.fbx)
    name, cm_per_unit = args.name, args.cm_per_unit
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

    # Peça sem trilha num clip (o object_15 do Koru só existe na ação 0x15) fica parada na pose do
    # seu nó do glb nesse clip: guarda essa pose para voltar a ela.
    node_pose = {}
    for n, o in parts.items():
        o.animation_data_create().use_nla = False
        node_pose[n] = o.matrix_basis.copy()

    def play(src):
        for n, o in parts.items():
            if slot_of[n] in src.slots:
                o.animation_data.action = src
                o.animation_data.action_slot = src.slots[slot_of[n]]
            else:
                o.animation_data.action = None
                o.matrix_basis = node_pose[n]

    loop_names = {c.strip().lower() for c in args.loop_clips.split(",") if c.strip()}
    slot_of = {n: o.animation_data.action_slot.identifier if o.animation_data.action_slot else "OB" + o.name
               for n, o in parts.items()}
    all_slots = list(slot_of.values())
    for a in clips:
        if a.name.split(" / ")[0].lower() in loop_names:
            slots = [s for s in a.slots if s.identifier in all_slots]
            if close_loop(a, slots):
                log("  ciclo fechado: %s" % a.name)

    # Pose de descanso = quadro 0 do primeiro clip (o exportador usa o idle como base).
    idle = next((a for a in clips if a.name.lower() == "idle"), clips[0])
    play(idle)

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

    # Com o bloco: cada vértice soldado guarda (objeto, índice no TMD), para receber os deltas dos
    # clip morphs e para cada face achar o seu primitivo (semi-transparência). As posições do
    # .glb são as do TMD (monster_gltf.rs), então o vértice se acha pela posição.
    morph_data = read_clip_morphs(os.path.abspath(args.block)) if args.block else None
    face_blends = read_face_blends(os.path.abspath(args.block)) if args.block else {}
    if morph_data and morph_data[2] and scn.render.fps != MONSTER_KEY_FPS * 2:
        raise RuntimeError("clip morphs só nos monstros (glb a 1/%d s)" % MONSTER_KEY_FPS)
    if morph_data and (morph_data[2] or face_blends):
        tmd_vertices = morph_data[0]
        for n, o in parts.items():
            lookup = {gltf_to_blender(v): k for k, v in reversed(list(enumerate(tmd_vertices[n])))}
            index = [lookup.get(tuple(int(round(c)) for c in v.co), -1) for v in o.data.vertices]
            missing = index.count(-1)
            if missing:
                log("  [!] peça %d: %d vértice(s) sem par no TMD (ficam sem morph)" % (n, missing))
            for attr_name, values in (("tmd_obj", [n] * len(index)), ("tmd_vi", index)):
                attr = o.data.attributes.new(attr_name, 'INT', 'POINT')
                attr.data.foreach_set("value", values)
    else:
        morph_data = None
        face_blends = {}

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
    rest_basis = {}      # peça -> rotação+escala de repouso: leva o delta do TMD para a malha
    for n, o in sorted(parts.items()):
        rest_basis[n] = o.matrix_world.to_3x3()
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

    # --- clip morphs -> uma shape key por lane, com o peso vindo de uma propriedade da
    # armature (Morph_NN) por driver: a propriedade é animada em cada clip, e o FBX leva o peso
    # da shape key em cada take, porque o exportador amostra as shape keys a cada quadro.
    morph_props = []
    if morph_data:
        from mathutils import Vector
        _, lanes_by_label, morphs = morph_data
        objs = [0] * len(mesh.data.vertices)
        vis = [0] * len(mesh.data.vertices)
        mesh.data.attributes["tmd_obj"].data.foreach_get("value", objs)
        mesh.data.attributes["tmd_vi"].data.foreach_get("value", vis)
        where = {}
        for i, (obj, vi) in enumerate(zip(objs, vis)):
            if vi >= 0:
                where.setdefault((obj, vi), []).append(i)
        for attr_name in ("tmd_obj", "tmd_vi"):
            mesh.data.attributes.remove(mesh.data.attributes[attr_name])

        # Semi-transparência: cada face vai para o slot do seu primitivo (achado pelos índices
        # do TMD dos seus cantos). Sem nenhuma face semi-transparente, fica o slot único de sempre.
        if face_blends:
            slot_of_face = []
            for poly in mesh.data.polygons:
                obj = objs[poly.vertices[0]]
                trio = frozenset(vis[v] for v in poly.vertices)
                slot_of_face.append(face_blends.get(obj, {}).get(trio, 0))
            used = sorted(set(slot_of_face))
            mesh.data.materials.clear()
            for s in used:
                mesh.data.materials.append(bpy.data.materials.get(BLEND_SLOTS[s]) or bpy.data.materials.new(BLEND_SLOTS[s]))
            mesh.data.polygons.foreach_set("material_index", [used.index(s) for s in slot_of_face])
            log("  materiais: " + ", ".join("%s %d face(s)" % (BLEND_SLOTS[s], slot_of_face.count(s)) for s in used))

        if morphs:
            mesh.shape_key_add(name="Basis", from_mix=False)
        for ix, (keys, records) in sorted(morphs.items()):
            prop = "Morph_%02d" % ix
            kb = mesh.shape_key_add(name=prop, from_mix=False)
            moved = 0
            for obj, first, deltas in records:
                basis = rest_basis.get(obj)
                if basis is None:
                    continue
                for k, d in enumerate(deltas):
                    for i in where.get((obj, first + k), ()):
                        kb.data[i].co = mesh.data.vertices[i].co + basis @ Vector(gltf_to_blender(d))
                        moved += 1
            arm[prop] = 0.0
            fc = kb.driver_add("value")
            fc.driver.type = 'AVERAGE'
            var = fc.driver.variables.new()
            var.type = 'SINGLE_PROP'
            var.targets[0].id_type = 'OBJECT'
            var.targets[0].id = arm
            var.targets[0].data_path = '["%s"]' % prop
            morph_props.append((ix, prop, keys))
            log("  morph %s: envelope %s, %d vértice(s)" % (prop, "-".join(map(str, keys)), moved))

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
        play(src)
        last = int(math.ceil(src.frame_range[1] - 1e-3))
        arm.animation_data.action = None
        bpy.ops.nla.bake(frame_start=0, frame_end=last, only_selected=True, visual_keying=True,
                         clear_constraints=False, use_current_action=False, bake_types={'POSE'})
        act = arm.animation_data.action
        act.name = "AS_%s_%s" % (name, clip_suffix(src.name))
        act.use_fake_user = True
        baked.append((act.name, last))

        # Peso de cada lane neste clip, quadro a quadro. Toda Morph_NN entra em todo clip (zero
        # nos que não a usam): sem curva, o valor do clip anterior ficaria valendo no take.
        # Quadro b do Blender = quadro b/2 do PS1 = phase 8*b em 12.4.
        if morph_props:
            mine = set(morph_data[1].get(src.name, []))
            for ix, prop, keys in morph_props:
                fc = act.fcurve_ensure_for_datablock(arm, '["%s"]' % prop)
                values = [clip_morph_weight(keys, 8 * b) if ix in mine else 0.0 for b in range(last + 1)]
                fc.keyframe_points.clear()
                fc.keyframe_points.add(len(values))
                fc.keyframe_points.foreach_set("co", [c for b, w in enumerate(values) for c in (b, w)])
                fc.keyframe_points.foreach_set("interpolation", [1] * len(values))    # LINEAR
                fc.update()      # sem isto a curva avalia errado (pontos fora de ordem/sem handles)
            if mine:
                log("  %s: morphs %s" % (act.name, ", ".join("Morph_%02d" % ix for ix in sorted(mine))))
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
