"""
ps1_equip_pieces.py — corta as peças de equipamento da forma de batalha da party (uma malha por
seção de equipamento, no MESMO esqueleto do SK_<Nome>_Battle), prontas para o Unreal.

Roda DENTRO do Blender (5.2+), sem interface:

    blender -b --factory-startup --python Extras/ps1_equip_pieces.py -- \
        <arts.glb> <equip-site/<nome>> <saida> <Nome> [--cm-per-unit X] [--only Weapon,RaSeru]

  <arts.glb>          o glb da página Arts do site do RE: o mesmo que gerou o SK_<Nome>_Battle
                      (Extras/ps1_glb_to_fbx.py). Daqui sai o esqueleto, idêntico ao importado.
  <equip-site/<nome>> uma pasta com default.glb + sN_ID_nome.glb, um personagem montado pelo jogo
                      com UM item equipado e o resto no padrão (só malha + clip Idle), e o
                      manifest.json da pasta de cima. Gerada na página Characters do site do RE
                      (set_equipped_character + equipped_character_glb, do wasm do site).

Por que peças, e por que assim (RE: battle-data-pack.md, "Every section is bone geometry"):
  - O jogo não tem malha de item. Cada seção de equipamento troca OBJETOS INTEIROS do esqueleto
    (o Survival Knife troca a mão direita por outra mão, com lâmina). Cada seção é sempre o mesmo
    grupo de objetos (SECTIONS abaixo), então o personagem é a soma de 5 peças, uma por seção.
  - Uma peça é o grupo de objetos da seção, tirado do personagem montado com aquele item; a peça
    padrão sai do default.glb. Cada objeto vai 100% no bone part_NN dele, como no conversor do
    personagem — então qualquer AnimSequence do SK_<Nome>_Battle anima qualquer peça.
  - A pose de repouso é o quadro 0 do Idle (a mesma do ps1_glb_to_fbx); o script confere que a
    pose do arquivo do item é a do arts.glb antes de usá-la.
  - A textura de cada peça é o atlas do personagem montado com aquele item: cada item sobe as
    suas próprias páginas e paletas para a VRAM.

Saída em <saida>: <Nome>_<Seção>_<Item>.fbx + T_<Nome>_<Seção>_<Item>.png por peça (o padrão é
<Nome>_<Seção>_Default) e pieces.json (seção, item do DT_Items, objetos, arquivos).

Conteúdo extraído do disco é da Sony: entradas e saídas ficam FORA do repositório.
"""
import argparse
import json
import os
import re
import sys

import bpy
import bmesh

DEFAULT_CM_PER_UNIT = 0.433005
WELD_DISTANCE = 0.01
POSE_TOLERANCE_CM = 0.05

# Seção do arquivo do jogador -> (nome da peça, ossos do esqueleto). Medido no site do RE (os
# objetos que mudam em relação ao padrão, somando todos os itens da seção) e conferido com o
# export-glb --items. As seções repartem os ossos do esqueleto (Vahn e Gala 15, Noa 16). Noa tem
# as seções 2 e 3 trocadas: é canhota, arma na mão esquerda e Ra-Seru na direita.
SECTIONS = {
    "Vahn": {0: ("Body", [0, 1]), 1: ("Head", [2]), 2: ("Weapon", [3, 4, 5]),
             3: ("RaSeru", [6, 7, 8]), 4: ("Feet", [9, 10, 11, 12, 13, 14])},
    "Noa":  {0: ("Body", [0, 1]), 1: ("Head", [2, 3]), 2: ("RaSeru", [4, 5, 6]),
             3: ("Weapon", [7, 8, 9]), 4: ("Feet", [10, 11, 12, 13, 14, 15])},
    "Gala": {0: ("Body", [0, 1]), 1: ("Head", [2]), 2: ("Weapon", [3, 4, 5]),
             3: ("RaSeru", [6, 7, 8]), 4: ("Feet", [9, 10, 11, 12, 13, 14])},
}

# Itens cujo nome no disco não é o do DT_Items: (personagem, id do PS1) -> chave.
ITEM_KEYS = {
    ("Noa", 28): "noa_feral",
    ("Gala", 32): "gala_mace",
    ("Gala", 63): "power_earrings",
}


def log(msg):
    print("[ps1_equip_pieces] " + msg)


def camel(text):
    """'ra_seru_armor' -> 'RaSeruArmor', 'Ra-Seru Meta Lv1' -> 'RaSeruMetaLv1'."""
    return "".join(w[:1].upper() + w[1:] for w in re.split(r"[^A-Za-z0-9]+", text or "") if w)


def item_key(name):
    """A chave do DT_Items para o nome do item ('Hunter Clothes' -> 'hunter_clothes',
    "Fighter's Band" -> 'fighters_band')."""
    return re.sub(r"[^a-z0-9]+", "_", (name or "").lower().replace("'", "")).strip("_")


def host_of(extra, parts, bones):
    """O osso que um objeto extra acompanha. Os extras do jogo (as peças além do esqueleto: o
    item solto da Noa, as variantes do Ra-Seru) não têm canal de animação próprio: andam no do
    osso que os precede na seção (RE: character-mesh.md, anm_bones). No quadro 0 do Idle a pose
    deles é a desse osso — é por ela que o dono se acha, porque o número do extra muda conforme
    o que está equipado."""
    m = parts[extra].matrix_world

    def distance(b):
        return ((parts[b].matrix_world.translation - m.translation).length
                + parts[b].matrix_world.to_quaternion().rotation_difference(m.to_quaternion()).angle)

    best = min(bones, key=distance)
    if distance(best) > POSE_TOLERANCE_CM:
        log("  [!] extra %d não coincide com osso nenhum (o mais perto: %d, a %.3f)"
            % (extra, best, distance(best)))
    return best


def import_glb(path):
    """Importa um glb com a cena em METRO (o importador do glTF converte metro -> unidade da
    cena: com a cena em cm os dados entrariam x100) e devolve os objetos novos."""
    scn = bpy.context.scene
    scn.unit_settings.scale_length = 1.0
    before = set(scn.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in scn.objects if o not in before]


def posed_parts(objs, cm_per_unit):
    """{índice do objeto: objeto} já no quadro 0 do Idle, com a escala no nó raiz."""
    scn = bpy.context.scene
    parts = {}
    for o in objs:
        if o.type == 'MESH':
            m = re.search(r"object_(\d+)", o.name)
            if m:
                parts[int(m.group(1))] = o
    for o in parts.values():
        ad = o.animation_data
        if not ad or not ad.action_slot:
            continue
        ad.use_nla = False
        # Cada import cria as suas actions (Idle, Idle.001...): o Idle é o desta importação, o
        # que tem o slot deste objeto.
        slot = ad.action_slot.identifier
        if ad.action is None or ad.action.name.split(".")[0].lower() != "idle":
            ad.action = next(a for a in bpy.data.actions
                             if a.name.split(".")[0].lower() == "idle" and slot in [s.identifier for s in a.slots]
                             and a.users > 0)
        ad.action_slot = ad.action.slots[slot]
    for o in objs:
        if o.type == 'EMPTY' and o.parent is None:
            o.scale = (cm_per_unit, cm_per_unit, cm_per_unit)
    scn.frame_set(0)
    bpy.context.view_layer.update()
    return parts


def remove(objs):
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    for a in [a for a in bpy.data.actions if a.users == 0]:
        bpy.data.actions.remove(a)
    for m in [m for m in bpy.data.meshes if m.users == 0]:
        bpy.data.meshes.remove(m)
    for i in [i for i in bpy.data.images if i.users == 0]:
        bpy.data.images.remove(i)


def build_armature(mats, unit):
    """Root + part_NN no quadro 0 do Idle: o mesmo esqueleto do Extras/ps1_glb_to_fbx.py."""
    scn = bpy.context.scene
    arm_data = bpy.data.armatures.new("Armature")
    arm = bpy.data.objects.new("Armature", arm_data)
    scn.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    root = arm_data.edit_bones.new("root")
    root.head = (0, 0, 0)
    root.tail = (0, 0, 40 * unit)
    for n in sorted(mats):
        eb = arm_data.edit_bones.new("part_%02d" % n)
        eb.head = (0, 0, 0)
        eb.tail = (0, 25 * unit, 0)
        eb.matrix = mats[n].normalized()
        eb.parent = root
    bpy.ops.object.mode_set(mode='OBJECT')
    return arm


def export_piece(arm, parts, objects, name, out_dir):
    """Junta os objetos da seção numa malha presa a <arm> e exporta <name>.fbx. <objects> são
    pares (objeto, bone): cada objeto vai 100% no part_NN do seu bone (o próprio, ou o dono, se
    for um extra). Devolve o nº de vértices, ou 0 se o arquivo não tem nenhum dos objetos."""
    scn = bpy.context.scene
    copies = []
    for n, bone in objects:
        o = parts.get(n)
        if o is None:
            continue
        me = o.data.copy()
        me.transform(o.matrix_world)
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=WELD_DISTANCE)
        bm.to_mesh(me)
        bm.free()
        c = bpy.data.objects.new("tmp_%02d" % n, me)
        scn.collection.objects.link(c)
        vg = c.vertex_groups.new(name="part_%02d" % bone)
        vg.add(range(len(me.vertices)), 1.0, 'REPLACE')
        copies.append(c)
    if not copies:
        return 0
    for o in scn.objects:
        o.select_set(o in copies)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    mesh = copies[0]
    mesh.name = mesh.data.name = name
    mesh.parent = arm
    mesh.modifiers.new("Armature", 'ARMATURE').object = arm
    verts = len(mesh.data.vertices)

    # Cena em cm só para exportar (os dados já estão em cm): sem escala em nó nenhum do FBX.
    scn.unit_settings.system = 'METRIC'
    scn.unit_settings.scale_length = 0.01
    for o in scn.objects:
        o.select_set(o in (arm, mesh))
    bpy.ops.export_scene.fbx(
        filepath=os.path.join(out_dir, name + ".fbx"), use_selection=True,
        object_types={'ARMATURE', 'MESH'}, global_scale=1.0, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE', add_leaf_bones=False, use_armature_deform_only=False,
        mesh_smooth_type='OFF', use_mesh_modifiers=False, bake_anim=False, path_mode='COPY',
        embed_textures=False)
    bpy.data.objects.remove(mesh, do_unlink=True)
    return verts


def save_texture(objs, path):
    for o in objs:
        if o.type != 'MESH':
            continue
        for slot in o.material_slots:
            mat = slot.material
            if not mat or not mat.node_tree:
                continue
            for node in mat.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image and node.image.size[0] > 0:
                    node.image.filepath_raw = path
                    node.image.file_format = 'PNG'
                    node.image.save()
                    return True
    return False


def main(argv):
    ap = argparse.ArgumentParser(prog="ps1_equip_pieces")
    ap.add_argument("arts_glb")
    ap.add_argument("equip_dir")
    ap.add_argument("out_dir")
    ap.add_argument("name")
    ap.add_argument("--cm-per-unit", type=float, default=DEFAULT_CM_PER_UNIT)
    ap.add_argument("--only", default="", help="só estas seções (nomes de SECTIONS), ex.: Weapon,RaSeru")
    args = ap.parse_args(argv)
    name, unit = args.name, args.cm_per_unit
    if name not in SECTIONS:
        raise RuntimeError("sem tabela de seções para %s (SECTIONS)" % name)
    sections = SECTIONS[name]
    only = {s.strip() for s in args.only.split(",") if s.strip()}
    equip_dir = os.path.abspath(args.equip_dir)
    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    manifest = json.load(open(os.path.join(os.path.dirname(equip_dir), "manifest.json"), encoding="utf-8"))
    rows = [m for m in manifest if m["character"] == name.lower() and not m.get("error")]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30
    arts = import_glb(os.path.abspath(args.arts_glb))
    arts_parts = posed_parts(arts, unit)
    mats = {n: o.matrix_world.copy() for n, o in arts_parts.items()}
    remove(arts)
    arm = build_armature(mats, unit)

    bones = sorted(b for _, objects in sections.values() for b in objects)
    section_of_bone = {b: s for s, (_, objects) in sections.items() for b in objects}

    pieces = []
    for row in rows:
        section = row["section"]
        if section >= 0 and section not in sections:
            continue
        targets = sorted(sections) if section < 0 else [section]
        objs = import_glb(os.path.join(equip_dir, os.path.basename(row["file"])))
        parts = posed_parts(objs, unit)
        missing = [b for b in bones if b not in parts]
        if missing:
            raise RuntimeError("%s: faltam os objetos do esqueleto %s" % (row["file"], missing))
        drift = max((parts[b].matrix_world.translation - mats[b].translation).length for b in bones)
        if drift > POSE_TOLERANCE_CM:
            raise RuntimeError("%s: pose de repouso difere do arts.glb em %.3f cm" % (row["file"], drift))
        # Os extras (objetos além do esqueleto) vão na seção do osso que acompanham
        members = {s: [(b, b) for b in sections[s][1]] for s in sections}
        for n in sorted(parts):
            if n not in section_of_bone:
                host = host_of(n, parts, bones)
                members[section_of_bone[host]].append((n, host))
        for s in targets:
            label = sections[s][0]
            objects = members[s]
            if only and label not in only:
                continue
            if section < 0:
                item, key = "Default", ""
            elif label == "RaSeru":
                level = re.search(r"(\d+)$", row["name"] or "")
                item, key = ("Lv%s" % level.group(1)) if level else camel(row["name"]), ""
            else:
                if not row["name"]:
                    log("  pulado: %s (item sem nome)" % row["file"])
                    continue
                key = ITEM_KEYS.get((name, row["id"])) or item_key(row["name"])
                item = camel(key)
            piece = "%s_%s_%s" % (name, label, item)
            verts = export_piece(arm, parts, objects, piece, out_dir)
            if not verts:
                log("  [!] %s: nenhum dos objetos %s no arquivo" % (piece, objects))
                continue
            tex = os.path.join(out_dir, "T_%s.png" % piece)
            if not save_texture(objs, tex):
                log("  [!] %s: sem textura" % piece)
            pieces.append({"piece": piece, "section": s, "slot": label, "item": key,
                           "ps1_id": row["id"], "name": row["name"],
                           "objects": [n for n, _ in objects],
                           "extras": {str(n): b for n, b in objects if n != b},
                           "vertices": verts, "source": row["file"]})
            extras = ", ".join("%d->%d" % (n, b) for n, b in objects if n != b)
            log("  %-34s %4d vértices  objetos %s%s" % (piece, verts, [n for n, _ in objects],
                                                         ("  extras " + extras) if extras else ""))
        remove(objs)

    with open(os.path.join(out_dir, "pieces.json"), "w", encoding="utf-8") as f:
        json.dump(pieces, f, indent=1, ensure_ascii=False)
    log("%d peças em %s" % (len(pieces), out_dir))


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:])
