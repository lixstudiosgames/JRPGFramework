"""
ue_import_battle_voices.py — roda DENTRO do editor. Importa as vozes da luta que o Extras/ps1_battle_voices.py tirou do disco
(o grito das Arts, a fanfarra do SpecialStarter, o grunhido do block) e o som que a Super toca no lugar do grito.

No console do editor (modo Cmd):

    py "<repo>/Extras/ue_import_battle_voices.py" "<pasta Sounds do ps1_battle_voices>" [Vahn Noa Gala]

Destino (os caminhos que o Docs/Data/DT_BattleVoices.csv aponta):
  /Game/Legaia/Battle/Party/<Nome>/Voices/SW_<Nome>_Shout<cc> / _Fanfare<c> / _BlockGrunt
  /Game/Legaia/Battle/Sounds/Common/SW_Battle_Cue51 / CueAD / Cue58

Depois, importe Docs/Data/DT_BattleVoices.csv (Row Struct FJRPGBattleVoiceBankData) e Docs/Data/DT_ArtVoices.csv
(FJRPGArtVoiceData) em /Game/Data/. Os SoundWaves ficam 2D (voz da luta, ouvida em todo lugar). Rodar de novo substitui.
"""
import os
import sys
import traceback

import unreal

PARTY_ROOT = "/Game/Legaia/Battle/Party"
COMMON_DEST = "/Game/Legaia/Battle/Sounds/Common"
FINISHER_CUES = {"Vahn": "SW_Battle_Cue51", "Noa": "SW_Battle_CueAD", "Gala": "SW_Battle_Cue58"}


def import_wavs(folder, dest, only=None):
    tasks = []
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith(".wav") or (only is not None and name[:-4] not in only):
            continue
        task = unreal.AssetImportTask()
        task.filename = os.path.join(folder, name)
        task.destination_path = dest
        task.destination_name = name[:-4]
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    paths = [p for task in tasks for p in task.imported_object_paths]
    for path in paths:
        sound = unreal.EditorAssetLibrary.load_asset(str(path))
        if isinstance(sound, unreal.SoundWave):
            sound.set_editor_property("attenuation_settings", None)
            sound.set_editor_property("volume", 1.0)
            unreal.EditorAssetLibrary.save_loaded_asset(sound)
    return len(paths)


def main(argv):
    if not argv:
        unreal.log_error("uso: py ue_import_battle_voices.py <pasta Sounds> [Vahn Noa Gala]")
        return
    src = argv[0]
    names = argv[1:] or ["Vahn", "Noa", "Gala"]
    for name in names:
        try:
            folder = os.path.join(src, "Party", name, "Voices")
            own = import_wavs(folder, "%s/%s/Voices" % (PARTY_ROOT, name))
            common = import_wavs(os.path.join(src, "Battle", "Common"), COMMON_DEST, {FINISHER_CUES[name]})
            unreal.log("ue_import_battle_voices: %s — %d voz(es), %d comum(ns)" % (name, own, common))
        except Exception:
            unreal.log_error("%s: %s" % (name, traceback.format_exc()))


main(sys.argv[1:])
