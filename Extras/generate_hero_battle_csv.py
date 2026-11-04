#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_hero_battle_csv.py — gera as duas tabelas pequenas que a luta lê do disco para os heróis:

  Docs/Data/DT_RaSeruTiers.csv  (Row Struct: FJRPGRaSeruTierData)  o ATK do Ra-Seru de cada herói, por tier
  Docs/Data/DT_WeaponCosts.csv  (Row Struct: FJRPGWeaponCostData)  o custo do botão de Arms de cada arma

    python Extras/generate_hero_battle_csv.py --extracted <LegaiaRE/extracted> --tool <asset.exe> [--dry-run]

O Ra-Seru NÃO é um slot de equipamento aqui (o SeruLevel é um número), mas no original é o item do slot 3 — 24
linhas "Ra-Seru Meta $1..$9", "Ra-Seru Terra $1..$8", "Ra-Seru Ozma $1..$7" na tabela de equipamentos do
SCUS_942.54 (docs/formats/equipment-table.md, do legend-of-legaia-re). O ATK dele entra na luta como uma das
cinco peças do herói: a luta dobra ½ dele no golpe do botão Ra-Seru e em toda Art (BATTLE_ROADMAP.md › Damage
formula › Equipment fold). Meta é do Vahn, Terra da Noa, Ozma do Gala; o tier é o SeruLevel do herói. Os tiers não
trazem UDF, LDF nem SPD (todos 0 no disco), só o ATK.

O custo do botão de Arms está no arquivo de batalha de cada herói (PROT 863 Vahn, 864 Noa, 865 Gala): a seção de
cada arma tem, em [+0x04] + 0x74, o byte que o jogo desconta do gauge da AGL a cada botão de Arms — 30 (0x1E) a
classe favorita, 42 (0x2A) uma classe ao lado, 54 (0x36) bem fora (docs/subsystems/arts-command-gauge.md ›
"Disc location"). Os outros três botões custam sempre 30. A tabela só leva as armas que o herói pode equipar e cujo
custo não é 30; o resto usa UJRPGArtsRules::DefaultButtonCost. A key da arma é a do DT_Items (casada pelo nome).

Fontes (do disco do usuário, lidas com as ferramentas do RE; nada além dos CSVs entra no repositório — regra 48):
  - `asset item-tables <SCUS_942.54> --equipment-only`
  - `asset battle-data-pack <PROT 0863|0864|0865> --out <pasta temporária>`
"""

import argparse
import glob
import os
import re
import struct
import subprocess
import sys
import tempfile

import legaia_toml as lt

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

TIERS_TABLE = "DT_RaSeruTiers"
COSTS_TABLE = "DT_WeaponCosts"

# O Ra-Seru de cada herói e o arquivo de batalha dele
RASERU = {"Meta": "Vahn", "Terra": "Noa", "Ozma": "Gala"}
PACKS = {"Vahn": "0863", "Noa": "0864", "Gala": "0865"}

ITEM_RE = re.compile(r"^0x(?P<id>[0-9A-Fa-f]{2})\s+(?P<name>.+?)\s+equip\s+atk=(?P<atk>\d+)\s")
TIER_RE = re.compile(r"^Ra-Seru (?P<seru>Meta|Terra|Ozma) \$(?P<tier>\d+)$")
REC_RE = re.compile(r"^rec(?P<n>\d+)_id(?P<id>[0-9a-fA-F]+)\.bin$")


def run(tool, *args):
    return subprocess.run([tool, *args], check=True, capture_output=True, text=True, encoding="utf-8",
                          errors="replace").stdout


def parse_equipment(tool, scus):
    """{id: (nome, atk)} da tabela de equipamentos do SCUS."""
    out = {}
    for line in run(tool, "item-tables", scus, "--equipment-only").splitlines():
        m = ITEM_RE.match(line)
        if m:
            out[int(m["id"], 16)] = (m["name"].strip(), int(m["atk"]))
    return out


def arms_costs(tool, extracted, character, workdir):
    """{item id: custo do botão de Arms} do arquivo de batalha do herói."""
    matches = glob.glob(os.path.join(extracted, "PROT", PACKS[character] + "_*.BIN"))
    if not matches:
        sys.exit("Não achei PROT/%s_*.BIN em %s" % (PACKS[character], extracted))
    out_dir = os.path.join(workdir, character)
    os.makedirs(out_dir, exist_ok=True)
    run(tool, "battle-data-pack", matches[0], "--out", out_dir)
    costs = {}
    for name in sorted(os.listdir(out_dir)):
        m = REC_RE.match(name)
        if not m:
            continue
        with open(os.path.join(out_dir, name), "rb") as f:
            data = f.read()
        item_id = int(m["id"], 16)
        # Só as seções de arma têm o registro de golpe; as outras trazem [+0x04] = 0
        swing = struct.unpack_from("<I", data, 4)[0]
        if item_id and swing and swing + 0x74 < len(data):
            costs[item_id] = data[swing + 0x74]
    return costs


def main():
    ap = argparse.ArgumentParser(prog="generate_hero_battle_csv")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract (PROT/, SCUS_942.54)")
    ap.add_argument("--tool", required=True, help="o asset.exe do RE (v1.10.0 ou mais novo)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    scus = os.path.join(args.extracted, "SCUS_942.54")
    equipment = parse_equipment(args.tool, scus)
    if not equipment:
        sys.exit("`asset item-tables --equipment-only` não devolveu nada — o SCUS_942.54 é o da versão USA?")
    items, item_order = lt.read_rows("DT_Items")
    by_name = {row["Name"].strip().lower(): key for key, row in items.items()}
    report = {"arma sem key no DT_Items": [], "tier fora do padrão": []}

    # --- DT_RaSeruTiers -----------------------------------------------------------------------------
    tiers = {}
    for _id, (name, atk) in sorted(equipment.items()):
        m = TIER_RE.match(name)
        if m:
            tiers.setdefault(RASERU[m["seru"]], {})[int(m["tier"])] = atk
    tier_rows = []
    for character in ("Vahn", "Noa", "Gala"):
        by_tier = tiers.get(character, {})
        atk = [by_tier[t] for t in sorted(by_tier)]
        if sorted(by_tier) != list(range(1, len(atk) + 1)):
            report["tier fora do padrão"].append("%s: %s" % (character, sorted(by_tier)))
        tier_rows.append({"---": character, "Character": character, "TierATK": lt.ue_array(atk)})

    # --- DT_WeaponCosts -----------------------------------------------------------------------------
    cost_rows = []
    with tempfile.TemporaryDirectory(prefix="legaia_packs_") as workdir:
        for character in ("Vahn", "Noa", "Gala"):
            costs = arms_costs(args.tool, args.extracted, character, workdir)
            for item_id, cost in sorted(costs.items()):
                if cost == 30 or item_id not in equipment:
                    continue
                name = equipment[item_id][0]
                key = by_name.get(name.lower())
                if not key:
                    report["arma sem key no DT_Items"].append("%s: %s (0x%02X, %d)" % (character, name, item_id, cost))
                    continue
                row = items[key]
                owners = [row.get("EquipBest", "")] + re.findall(r'"([^"]+)"', row.get("EquipOthers", ""))
                if character not in owners:
                    continue   # a seção existe no arquivo mas o herói não equipa a arma
                cost_rows.append({"---": "%s_%s" % (character, key), "Character": character, "Item": key,
                                  "ArmsCost": cost})

    print("[hero-battle] DT_RaSeruTiers: %s" % ", ".join(
        "%s %d tiers (%s)" % (r["Character"], r["TierATK"].count(",") + 1, r["TierATK"]) for r in tier_rows))
    print("[hero-battle] DT_WeaponCosts: %d armas com custo diferente de 30" % len(cost_rows))
    for r in cost_rows:
        print("            %-26s %d" % (r["---"], r["ArmsCost"]))
    for title, lines in report.items():
        if lines:
            print("[hero-battle] %s: %d" % (title, len(lines)))
            for line in lines:
                print("            " + line)
    if args.dry_run:
        print("[hero-battle] --dry-run: nada escrito.")
        return
    for table, columns, rows in ((TIERS_TABLE, ["Character", "TierATK"], tier_rows),
                                 (COSTS_TABLE, ["Character", "Item", "ArmsCost"], cost_rows)):
        bak = lt.backup(table)
        if bak:
            print("[hero-battle] backup: " + bak)
        lt.write_rows(table, columns, rows)
        print("[hero-battle] OK: " + lt.csv_path(table))


if __name__ == "__main__":
    main()
