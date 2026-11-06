"""
ue_import_battle_music.py — roda DENTRO do editor. Importa as músicas de luta que o Extras/ps1_battle_music.py tirou do disco
(o par Intro / Loop de cada faixa do banco de luta).

No console do editor (modo Cmd):

    py "<repo>/Extras/ue_import_battle_music.py" "<pasta Music/Battle do ps1_battle_music>" [/Game/destino]

Destino padrão: /Game/Legaia/sounds/OST/Battle/BGM_<id>_<Nome>_Intro / _Loop. O Loop sai com Looping ligado, o Intro não;
os dois 2D, sem atenuação. Rodar de novo substitui.
"""
import json
import os
import sys

import unreal

DEFAULT_DEST = "/Game/Legaia/sounds/OST/Battle"


def main(argv):
    if not argv:
        unreal.log_error("uso: py ue_import_battle_music.py <pasta Music/Battle> [/Game/destino]")
        return
    src = argv[0]
    dest = argv[1] if len(argv) > 1 else DEFAULT_DEST
    manifest = json.load(open(os.path.join(src, "music.json"), encoding="utf-8"))
    tasks = []
    for track in manifest:
        for name in (track["intro"], track["loop"]):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(src, name + ".wav")
            task.destination_path = dest
            task.destination_name = name
            task.automated = True
            task.replace_existing = True
            task.save = True
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    count = 0
    for track in manifest:
        for name, looping in ((track["intro"], False), (track["loop"], True)):
            sound = unreal.EditorAssetLibrary.load_asset("%s/%s" % (dest, name))
            if not isinstance(sound, unreal.SoundWave):
                unreal.log_warning("ue_import_battle_music: %s não importou" % name)
                continue
            sound.set_editor_property("looping", looping)
            sound.set_editor_property("attenuation_settings", None)
            unreal.EditorAssetLibrary.save_loaded_asset(sound)
            count += 1
    unreal.log("ue_import_battle_music: %d SoundWave(s) em %s" % (count, dest))


main(sys.argv[1:])
