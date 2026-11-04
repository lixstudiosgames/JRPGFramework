#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_enemy_actions_csv.py — gera Docs/Data/DT_EnemyActions.csv (Row Struct: FJRPGEnemyActionData,
Battle/EnemyActionData.h): uma linha por clip (por entrada de ação) de cada monstro do DT_Enemies.

    python Extras/generate_enemy_actions_csv.py --archive <extracted/PROT/0867_battle_data.BIN> \
        --tool <asset.exe> [--manifest <monsters.json>] [--dry-run]

O que a luta precisa de cada ação do monstro e que não é um notify: os bytes de poder (um por golpe),
os quadros de golpe (a lista de eventos +0x10..+0x13), o custo de AGL (+0x74), o rate (+0x78), o
seletor de status (+0x7A), o gate (+0x76) e o tamanho do clip. É a própria entrada de ação do disco
(docs/formats/monster-animation.md, do legend-of-legaia-re); a linha lógica da luta nasce daqui, e
nada depende de um clip tocar (BATTLE_ROADMAP.md › 5.0 › The logic timeline).

Fontes:
  - Docs/Data/DT_Enemies.csv: quais monstros e o MonsterArchiveID de cada um;
  - o arquivo de monstros do disco (PROT 0867), lido com as ferramentas do RE
    (`asset monster-archive <0867> --id N --dump-block`, uma vez por monstro) — o bloco decodificado
    fica numa pasta temporária e não entra no repositório;
  - opcional, o monsters.json do lote de modelos (Extras/ps1_monsters_batch.py): o nome de cada clip
    e, com ele, o caminho do UAnimSequence no Unreal (a mesma convenção do generate_enemy_anims_csv.py).

O nome da linha é <EnemyKey>_<NN>_<Clip> (NN = o índice da entrada no arquivo). Linhas criadas à mão
ou no LegaiaStudio (de um monstro que não está no DT_Enemies) ficam; as dos monstros do DT_Enemies são
reescritas. Um Anim posto à mão numa linha que o lote não conhece é mantido. O CSV anterior vai para
Docs/Data/_backup/.
"""

import argparse
import sys

import legaia_toml as lt
import monster_block as mb

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

TABLE = "DT_EnemyActions"
ROOT = "/Game/Legaia/Battle/Enemies"
COLUMNS = ["Enemy", "ArchiveID", "ActionIndex", "Tag", "Clip", "Anim", "Power", "HitFrames",
           "AGLCost", "Gate", "Rate", "StatusSelector", "FrameCount"]
# Preenchidas fora do gerador quando o lote não conhece o monstro: mantidas se já existiam
MANUAL_COLUMNS = ["Anim"]


def asset(name, asset_name):
    return "%s/%s/%s.%s" % (ROOT, name, asset_name, asset_name)


def clip_names(manifest_entry, actions):
    """O nome do clip de cada entrada: o do lote (na ordem do arquivo) ou Action_0x<tag>."""
    clips = manifest_entry["clips"] if manifest_entry else []
    ok = len(clips) == len(actions) and all(c["action"] == a["tag"] for c, a in zip(clips, actions))
    names = []
    for a in actions:
        name = "Action_0x%02X" % a["tag"]
        if ok:
            name = clips[a["index"]]["clip"]
        names.append(name)
    return names, ok


def main():
    ap = argparse.ArgumentParser(prog="generate_enemy_actions_csv")
    ap.add_argument("--archive", required=True, help="extracted/PROT/0867_battle_data.BIN")
    ap.add_argument("--tool", required=True, help="o asset.exe do RE (v1.10.0 ou mais novo)")
    ap.add_argument("--manifest", help="monsters.json do lote (nomes dos clips e caminhos do Unreal)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    manifest = {}
    if args.manifest:
        import json
        with open(args.manifest, encoding="utf-8") as f:
            manifest = json.load(f)

    enemies, enemy_order = lt.read_rows("DT_Enemies")
    old_rows, old_order = lt.read_rows(TABLE)
    report = {"sem MonsterArchiveID": [], "clips do lote não casam com as entradas": [],
              "ataque sem golpe (tag 0x0C..0x1F, custo real, sem quadro)": [],
              "Anim mantido do CSV (posto à mão)": []}

    rows = []
    with mb.BlockReader(args.tool, args.archive) as reader:
        for key in enemy_order:
            e = enemies[key]
            monster_id = int(e.get("MonsterArchiveID", "-1") or -1)
            if monster_id < 0:
                report["sem MonsterArchiveID"].append(key)
                continue
            record = reader.read(monster_id)
            entry = manifest.get(str(monster_id))
            names, matched = clip_names(entry, record["actions"])
            if entry and not matched:
                report["clips do lote não casam com as entradas"].append("%s (id %d)" % (key, monster_id))
            for a, clip in zip(record["actions"], names):
                row_key = "%s_%02d_%s" % (key, a["index"], clip)
                anim = ""
                if entry and matched:
                    anim = asset(entry["name"], "AS_%s_%s" % (entry["name"], clip))
                if 0x0C <= a["tag"] <= 0x1F and a["agl_cost"] != 0xFF and not a["hit_frames"]:
                    report["ataque sem golpe (tag 0x0C..0x1F, custo real, sem quadro)"].append(row_key)
                rows.append({
                    "---": row_key, "Enemy": key, "ArchiveID": monster_id,
                    "ActionIndex": a["index"], "Tag": a["tag"], "Clip": clip, "Anim": anim,
                    "Power": lt.ue_array(a["power"]), "HitFrames": lt.ue_array(a["hit_frames"]),
                    "AGLCost": a["agl_cost"], "Gate": a["gate"], "Rate": a["rate"],
                    "StatusSelector": a["status"], "FrameCount": a["frames"],
                })

    # Trabalho manual sobrevive à regeneração
    generated = {r["---"] for r in rows}
    for r in rows:
        old = old_rows.get(r["---"])
        for c in MANUAL_COLUMNS:
            if old and not r[c] and old.get(c):
                r[c] = old[c]
                report["Anim mantido do CSV (posto à mão)"].append(r["---"])
    custom = [old_rows[k] for k in old_order if k not in generated and old_rows[k].get("Enemy") not in enemies]
    rows += custom

    print("[enemy-actions] %d linhas de %d monstros (%d criadas fora do gerador)"
          % (len(rows), len(enemy_order), len(custom)))
    for title, lines in report.items():
        if lines:
            print("[enemy-actions] %s: %d" % (title, len(lines)))
            for line in lines[:12]:
                print("            " + line)
            if len(lines) > 12:
                print("            … (+%d)" % (len(lines) - 12))
    if args.dry_run:
        print("[enemy-actions] --dry-run: nada escrito.")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[enemy-actions] backup: " + bak)
    lt.write_rows(TABLE, COLUMNS, rows)
    print("[enemy-actions] OK: " + lt.csv_path(TABLE))


if __name__ == "__main__":
    main()
