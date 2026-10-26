"""
ue_import_monster_sounds.py — roda DENTRO do editor. Importa os sons de um monstro (e os comuns
que ele usa) e põe um AnimNotify_PlaySound em cada clip dele, no quadro certo, a partir do
sounds.json do Extras/ps1_monster_sounds.py.

No console do editor (modo Cmd):

    py "<repo>/Extras/ue_import_monster_sounds.py" "<pasta Sounds do ps1_monster_sounds>" Gimard [Drake_Ghost ...]

Destino (Docs/Guides/PS1_ASSETS.md):
  /Game/Legaia/Battle/Enemies/<Nome>/Sounds/SW_<Nome>_<Uso>   os sons do monstro
  /Game/Legaia/Battle/Sounds/Common/SW_Battle_Cue<NN>          os comuns (só os que ele usa)
  trilha de notify "Sounds" em cada /Game/Legaia/Battle/Enemies/<Nome>/AS_<Nome>_<Clip>
Os SoundWaves ficam 2D (batalha). No Field o AJRPGFieldEnemy os toca em 3D, com raio curto.

Rodar de novo refaz a trilha "Sounds" do zero (não duplica) e substitui os SoundWaves.
"""
import json
import os
import sys
import traceback

import unreal

ENEMIES_ROOT = "/Game/Legaia/Battle/Enemies"
COMMON_DEST = "/Game/Legaia/Battle/Sounds/Common"
TRACK = "Sounds"

def apply_sound_settings(paths):
    """Os SoundWaves são os da batalha: 2D, ouvidos em todo lugar, no volume que o WAV já traz (o
    do jogo). No Field, o AJRPGFieldEnemy toca os mesmos sons em 3D, com raio curto."""
    for path in paths:
        sound = unreal.EditorAssetLibrary.load_asset(str(path))
        if not isinstance(sound, unreal.SoundWave):
            continue
        sound.set_editor_property("attenuation_settings", None)
        sound.set_editor_property("volume", 1.0)
        unreal.EditorAssetLibrary.save_loaded_asset(sound)


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
    apply_sound_settings(paths)
    return len(paths)


def clear_sound_notifies(anim):
    """Refaz do zero: tira as trilhas que só têm PlaySound (as deste script, ou as que a engine
    criou para sons simultâneos). Uma trilha com outro notify fica — é de alguém."""
    lib = unreal.AnimationLibrary
    for track in [str(t) for t in lib.get_animation_notify_track_names(anim)]:
        events = lib.get_animation_notify_events_for_track(anim, track)
        if any(not isinstance(e.notify, unreal.AnimNotify_PlaySound) for e in events):
            continue
        lib.remove_animation_notify_events_by_track(anim, track)
        if len(lib.get_animation_notify_track_names(anim)) > 1:
            lib.remove_animation_notify_track(anim, track)


def import_monster(src, monster):
    root = "%s/%s" % (ENEMIES_ROOT, monster)
    own_dest = root + "/Sounds"
    manifest = json.load(open(os.path.join(src, "Enemies", monster, "sounds.json"), encoding="utf-8"))
    imported = import_wavs(os.path.join(src, "Enemies", monster), own_dest)
    common_used = {n["sound"] for n in manifest["notifies"] if n["folder"] == "Battle/Common"}
    if common_used:
        imported += import_wavs(os.path.join(src, "Battle", "Common"), COMMON_DEST, common_used)

    by_clip = {}
    for n in manifest["notifies"]:
        by_clip.setdefault(n["clip"], []).append(n)

    lib = unreal.AnimationLibrary
    notifies, errors = 0, 0
    for clip, notes in sorted(by_clip.items()):
        anim = unreal.EditorAssetLibrary.load_asset("%s/%s" % (root, clip))
        if not anim:
            unreal.log_warning("%s: clip não encontrado: %s" % (monster, clip))
            errors += 1
            continue
        clear_sound_notifies(anim)
        # Dois sons no mesmo quadro não podem dividir a trilha: a engine empurra um deles para uma
        # trilha nova, sem nome. Então o 2º som simultâneo vai para "Sounds 2", e assim por diante.
        layer_at = {}
        for n in notes:
            dest = own_dest if n["folder"].startswith("Enemies") else COMMON_DEST
            sound = unreal.EditorAssetLibrary.load_asset("%s/%s" % (dest, n["sound"]))
            if not sound:
                unreal.log_warning("%s: som não encontrado: %s/%s" % (monster, dest, n["sound"]))
                errors += 1
                continue
            time = min(float(n["time"]), anim.get_play_length())
            layer = layer_at.get(n["frame"], 0)
            layer_at[n["frame"]] = layer + 1
            track = TRACK if layer == 0 else "%s %d" % (TRACK, layer + 1)
            if track not in [str(t) for t in lib.get_animation_notify_track_names(anim)]:
                lib.add_animation_notify_track(anim, track, unreal.LinearColor(0.9, 0.3, 0.2, 1.0))
            notify = lib.add_animation_notify_event(anim, track, time, unreal.AnimNotify_PlaySound)
            notify.set_editor_property("sound", sound)
            notify.set_editor_property("follow", True)   # acompanha o monstro andando
            notifies += 1
        # Trilhas que ficaram vazias (a engine exige ao menos uma)
        for track in [str(t) for t in lib.get_animation_notify_track_names(anim)]:
            if not lib.get_animation_notify_events_for_track(anim, track) and len(lib.get_animation_notify_track_names(anim)) > 1:
                lib.remove_animation_notify_track(anim, track)
        unreal.EditorAssetLibrary.save_loaded_asset(anim)

    unreal.log("ue_import_monster_sounds: %s — %d som(ns) importado(s), %d notify(s), %d erro(s)"
               % (monster, imported, notifies, errors))


def main(argv):
    if len(argv) < 2:
        unreal.log_error("uso: py ue_import_monster_sounds.py <pasta Sounds> <Nome> [<Nome> ...]")
        return
    src = argv[0]
    for monster in argv[1:]:
        try:
            import_monster(src, monster)
        except Exception:
            unreal.log_error("%s: %s" % (monster, traceback.format_exc()))


main(sys.argv[1:])
