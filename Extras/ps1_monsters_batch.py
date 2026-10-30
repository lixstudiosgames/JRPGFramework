"""
ps1_monsters_batch.py — todos os monstros do disco de uma vez: .glb + bloco pelo `asset` do RE,
FBX pelo Extras/ps1_glb_to_fbx.py (Blender sem interface, vários em paralelo) e um manifest com o
que cada um tem, para o import no Unreal e o DT_Enemies.

    python Extras/ps1_monsters_batch.py --extracted <LegaiaRE/extracted> --asset-exe <asset.exe> \
        --blender <blender.exe> --out <pasta> [--ids 10,16] [--jobs 6]

Saída (fora do repositório — conteúdo do disco):
  <out>/glb/NNN_<Nome>.glb, NNN_<Nome>.block    o export do RE e o bloco do monstro
  <out>/fbx/<Nome>.fbx, T_<Nome>.png            o que o ue_setup_monster.py --source importa
  <out>/fbx/<Nome>.log                          o log do conversor (só fica se falhar)
  <out>/monsters.json                           por id: nome, clips e quais são ataques; um bloco
                                                idêntico ao de um id menor leva same_as e o nome dele

Nome = o mesmo do Extras/ps1_monster_sounds.py ("Gimard $2" -> Gimard_Lv2; ids que repetem um
nome levam _<id>: Cort_181), então a pasta dos
sons e a do modelo batem. Os clips também têm os nomes dele (o exportador do RE: Idle, Move,
Damaged, KnockedDown, GettingUp, Block, Attack / Attack1.., Action_0xNN).

Ataques (o que vai em Anims.Attacks do DT_Enemies): as ações castáveis — id 0x0C..0x1F com custo
de AGL != 0xFF (RE: MonsterSpell::is_castable, as que a IA do jogo sorteia).

Rodar de novo refaz só o que falta (passe --force para refazer tudo).
"""
import argparse
import json
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps1_monster_sounds import action_entries, action_labels, clip_suffix, monster_name  # noqa: E402

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

CONVERTER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ps1_glb_to_fbx.py")


def run_asset(asset_exe, archive, monster_id, *args):
    return subprocess.run([asset_exe, "monster-archive", archive, "--id", str(monster_id)] + list(args),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def is_attack(entry):
    return 0x0C <= entry[0] <= 0x1F and entry[0x74] != 0xFF


def dump_block(args, archive, monster_id, glb_dir):
    """O caminho do bloco do monstro, ou None quando o slot está vazio (187..194)."""
    block_path = os.path.join(glb_dir, "%03d.block" % monster_id)
    if args.force or not os.path.isfile(block_path):
        run_asset(args.asset_exe, archive, monster_id, "--dump-block", block_path)
    return block_path if os.path.isfile(block_path) else None


def describe(block, monster_id):
    name = monster_name(block, monster_id)
    entries = action_entries(block)
    clips = [{"clip": clip_suffix(label), "action": e[0], "attack": is_attack(e)}
             for label, e in zip(action_labels(entries), entries)]
    return {"name": name, "clips": clips}


def export_glb(args, archive, monster_id, name, glb_dir):
    glb = os.path.join(glb_dir, "%03d_%s.glb" % (monster_id, name))
    if args.force or not os.path.isfile(glb):
        if not run_asset(args.asset_exe, archive, monster_id, "--glb", glb):
            raise RuntimeError("export do .glb falhou")
    return glb


def convert(args, name, glb, block, fbx_dir):
    fbx = os.path.join(fbx_dir, "%s.fbx" % name)
    if not args.force and os.path.isfile(fbx) and os.path.isfile(os.path.join(fbx_dir, "T_%s.png" % name)):
        return "pronto"
    log = os.path.join(fbx_dir, "%s.log" % name)
    with open(log, "w", encoding="utf-8") as f:
        code = subprocess.run([args.blender, "-b", "--factory-startup", "--python", CONVERTER, "--",
                               glb, fbx, name, "--block", block], stdout=f, stderr=subprocess.STDOUT).returncode
    if code != 0 or not os.path.isfile(fbx):
        raise RuntimeError("conversão falhou (ver %s)" % log)
    os.remove(log)
    return "convertido"


def main():
    ap = argparse.ArgumentParser(prog="ps1_monsters_batch")
    ap.add_argument("--extracted", required=True)
    ap.add_argument("--asset-exe", required=True)
    ap.add_argument("--blender", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ids", default="", help="só estes ids (padrão: os 194 slots)")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    args.asset_exe, args.blender = os.path.abspath(args.asset_exe), os.path.abspath(args.blender)

    archive = os.path.join(args.extracted, "PROT", "0867_battle_data.BIN")
    glb_dir, fbx_dir = os.path.join(args.out, "glb"), os.path.join(args.out, "fbx")
    os.makedirs(glb_dir, exist_ok=True)
    os.makedirs(fbx_dir, exist_ok=True)
    ids = [int(x) for x in args.ids.split(",") if x.strip()] or range(1, 195)

    manifest_path = os.path.join(args.out, "monsters.json")
    manifest = json.load(open(manifest_path, encoding="utf-8")) if os.path.isfile(manifest_path) else {}

    # Blocos idênticos byte a byte (Evil Bat 176-178 = 140, Cort 183-186 = 180) são o mesmo
    # monstro: não viram outra cópia, apontam para o menor id ("same_as").
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        blocks = dict(zip(ids, pool.map(lambda i: dump_block(args, archive, i, glb_dir), ids)))
    first_of, todo = {}, []
    for monster_id in sorted(i for i, b in blocks.items() if b):
        data = open(blocks[monster_id], "rb").read()
        same = first_of.setdefault(data, monster_id)
        if same != monster_id:
            manifest[str(monster_id)] = dict(manifest.get(str(same)) or describe(data, same), same_as=same)
            print("%3d = %d (bloco idêntico)" % (monster_id, same), flush=True)
            continue
        manifest[str(monster_id)] = describe(data, monster_id)
        todo.append(monster_id)

    def one(monster_id):
        name = manifest[str(monster_id)]["name"]
        try:
            glb = export_glb(args, archive, monster_id, name, glb_dir)
            return monster_id, convert(args, name, glb, blocks[monster_id], fbx_dir)
        except (RuntimeError, OSError) as e:
            return monster_id, "[!] %s" % e

    failed = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for monster_id, state in pool.map(one, todo):
            failed += state.startswith("[!]")
            print("%3d %-24s %s" % (monster_id, manifest[str(monster_id)]["name"], state), flush=True)
    manifest = {k: manifest[k] for k in sorted(manifest, key=int)}
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
    print("[batch] %d monstro(s) no manifest, %d falha(s)" % (len(manifest), failed))


if __name__ == "__main__":
    main()
