"""
ue_setup_monster.py — roda DENTRO do editor. Importa o FBX e a textura de um monstro (com
--source) e deixa a pasta como a do Gimard (Docs/Guides/PS1_ASSETS.md, passo 4):

  - com --source: <pasta>/<Nome>.fbx (malha, clips e morph targets) e <pasta>/T_<Nome>.png
  - clips SK_<Nome>_Anim_Armature_AS_<Nome>_<Clip> -> AS_<Nome>_<Clip>
  - T_<Nome>: Nearest, sem mipmaps, LOD group Pixels2D (o visual do PS1)
  - MI_<Nome> filho do M_PS1Monster com BaseTexture = T_<Nome>, no slot da malha; os slots
    PS1_Translucent / PS1_Additive (semi-transparência do PS1, do conversor com --block) levam
    MI_<Nome>_Translucent / _Additive, filhos do M_PS1MonsterTranslucent / Additive

No console do editor (modo Cmd):

    py "<repo>/Extras/ue_setup_monster.py" [--source "<exports/monsters>"] Gobu_Gobu [Evil_Fly ...]
    py "<repo>/Extras/ue_setup_monster.py" --source "<exports/monsters/all/fbx>" [--max 30] --all

--all: todo <Nome>.fbx da pasta que ainda não tem SK_<Nome> no projeto (o lote do
Extras/ps1_monsters_batch.py); os que já estão ficam. --max N: no máximo N por vez.

Pode rodar de novo: o que já está certo fica.
"""
import os
import sys
import traceback

import unreal

ENEMIES_ROOT = "/Game/Legaia/Battle/Enemies"
MASTER = "/Game/Legaia/Battle/Materials/M_PS1Monster"
# Slot do conversor -> (sufixo do MI, master). Semi-transparência do PS1: ABR 0 = média, ABR 1 = soma.
BLEND_MASTERS = (
    ("PS1_Translucent", "_Translucent", "/Game/Legaia/Battle/Materials/M_PS1MonsterTranslucent"),
    ("PS1_Additive", "_Additive", "/Game/Legaia/Battle/Materials/M_PS1MonsterAdditive"),
)


def import_monster(source, name):
    """Importa <source>/<Nome>.fbx (malha + clips + morph targets) e <source>/T_<Nome>.png.
    Pelo Interchange com o pipeline padrão e os morph targets ligados: o import_file do MCP os
    deixa de fora, e o slime é feito deles (clip morphs, Extras/ps1_glb_to_fbx.py --block)."""
    root = "%s/%s" % (ENEMIES_ROOT, name)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_morph_targets", True)
    material = pipeline.get_editor_property("material_pipeline")
    material.set_editor_property("import_materials", False)
    material.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)

    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    params.destination_name = "SK_%s" % name
    # A lista leva caminhos: o do pipeline em memória (transient) resolve enquanto ele existe
    params.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
    fbx = unreal.InterchangeManager.create_source_data("%s/%s.fbx" % (source, name))
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(root, fbx, params)
    manager.wait_until_all_tasks_done(False)     # o import do Interchange é assíncrono

    task = unreal.AssetImportTask()
    task.filename = "%s/T_%s.png" % (source, name)
    task.destination_path = root
    task.destination_name = "T_%s" % name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])


def setup(name):
    root = "%s/%s" % (ENEMIES_ROOT, name)
    lib = unreal.EditorAssetLibrary

    # Clips: o import do FBX prefixa o nome da malha e do armature
    marker = "AS_%s_" % name
    renamed = 0
    for path in lib.list_assets(root, recursive=False):
        asset_name = path.rsplit(".", 1)[-1]
        if not isinstance(lib.load_asset(path), unreal.AnimSequence) or asset_name.startswith(marker):
            continue
        at = asset_name.find(marker)
        if at < 0:
            unreal.log_warning("%s: clip fora do padrão, ficou como está: %s" % (name, asset_name))
            continue
        if lib.rename_asset(path.rsplit(".", 1)[0], "%s/%s" % (root, asset_name[at:])):
            renamed += 1

    # Textura no visual do PS1
    tex = lib.load_asset("%s/T_%s" % (root, name))
    if tex:
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_PIXELS2D)
        lib.save_loaded_asset(tex)
    else:
        unreal.log_warning("%s: sem T_%s na pasta" % (name, name))

    # Materiais: um MI por slot. O conversor (--block) separa as faces semi-transparentes do PS1
    # em slots PS1_Translucent (ABR 0, média) e PS1_Additive (ABR 1, soma); o resto é opaco.
    def material_instance(suffix, master):
        mi_name = "MI_%s%s" % (name, suffix)
        mi_path = "%s/%s" % (root, mi_name)
        mi = lib.load_asset(mi_path) if lib.does_asset_exist(mi_path) else None
        if not mi:
            mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                mi_name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, lib.load_asset(master))
        if tex:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, "BaseTexture", tex)
        lib.save_loaded_asset(mi)
        return mi

    mesh = lib.load_asset("%s/SK_%s" % (root, name))
    slots = []
    if mesh:
        # Por índice: o "for slot in mats" do Python dá cópias, e o slot ficava vazio
        mats = mesh.get_editor_property("materials")
        for i in range(len(mats)):
            slot = mats[i]
            slot_name = str(slot.get_editor_property("material_slot_name"))
            suffix, master = next(((s, m) for key, s, m in BLEND_MASTERS if key in slot_name), ("", MASTER))
            slot.set_editor_property("material_interface", material_instance(suffix, master))
            mats[i] = slot
            slots.append("%s=MI_%s%s" % (slot_name, name, suffix))
        mesh.set_editor_property("materials", mats)
        lib.save_loaded_asset(mesh)
    else:
        unreal.log_warning("%s: sem SK_%s na pasta" % (name, name))

    clips = sorted(p.rsplit(".", 1)[-1] for p in lib.list_assets(root, recursive=False)
                   if isinstance(lib.load_asset(p), unreal.AnimSequence))
    try:
        morphs = [str(m.get_name()) for m in mesh.get_editor_property("morph_targets")] if mesh else []
    except Exception:
        morphs = ["?"]
    unreal.log("ue_setup_monster: %s — %d clip(s) renomeado(s); clips: %s; morph targets: %s; materiais: %s"
               % (name, renamed, ", ".join(clips), ", ".join(morphs) or "nenhum", ", ".join(slots)))


def missing_in(source):
    """Os <Nome>.fbx da pasta que ainda não têm SK_<Nome> no projeto (o --all)."""
    lib = unreal.EditorAssetLibrary
    names = sorted(f[:-4] for f in os.listdir(source) if f.lower().endswith(".fbx"))
    return [n for n in names if not lib.does_asset_exist("%s/%s/SK_%s" % (ENEMIES_ROOT, n, n))]


def main(argv):
    source, limit = None, 0
    if argv[:1] == ["--source"]:
        source, argv = argv[1].replace("\\", "/"), argv[2:]
    if argv[:1] == ["--max"]:
        limit, argv = int(argv[1]), argv[2:]
    if argv == ["--all"] and source:
        argv = missing_in(source)
        unreal.log("ue_setup_monster: %d monstro(s) da pasta ainda sem malha no projeto" % len(argv))
        if not argv:
            return
        if limit:
            argv = argv[:limit]
    if not argv:
        unreal.log_error("uso: py ue_setup_monster.py [--source <pasta com <Nome>.fbx e T_<Nome>.png> [--max N] --all] "
                         "<Nome> [...]")
        return
    for name in argv:
        try:
            if source:
                import_monster(source, name)
            setup(name)
        except Exception:
            unreal.log_error("%s: %s" % (name, traceback.format_exc()))


main(sys.argv[1:])
