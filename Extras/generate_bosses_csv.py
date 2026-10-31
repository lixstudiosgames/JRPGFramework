#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_bosses_csv.py — gera Docs/Data/DT_Bosses.csv (Row Struct: FJRPGBossData,
Battle/BossFightData.h).

    python Extras/generate_bosses_csv.py [--dry-run]

Fonte: Docs/Data/gamedata/bosses.toml (do legend-of-legaia-re) — as 30 lutas de chefe do guia de
referência: 22 da história (B0001..B0014, um registro por chefe; o trio Delilas são três), a Lapis
(s0005) e 7 lutas do torneio da Muscle Dome. Os stats NÃO vêm daqui: estão no DT_Enemies
(bIsBoss); esta tabela é a camada da luta.

Enemies: os rows do DT_Enemies de quem entra, achados pelo nome — "Songi (1st)" -> songi_1st,
"Gaza (Sim-Seru)" -> gaza_sim_seru, "Vigurox2" -> viguro duas vezes, "Viguro Lv2 (Master)" ->
viguro_lv2; o torneio ("(Expert)", "(Master)") sai do nome e vai para Tournament. Quem não casar sai
no relatório e fica para resolver à mão.

Regenerar NÃO apaga trabalho manual: Enemies posto à mão é mantido, e linhas criadas no
LegaiaStudio (lutas novas) ficam. O CSV anterior vai para Docs/Data/_backup/.
"""

import argparse
import re
import sys

import legaia_toml as lt

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

TABLE = "DT_Bosses"
COLUMNS = ["Key", "DisplayName", "Location", "FightCode", "Enemies", "HP", "MP", "SpecialAttacks",
           "SpecialAttackMP", "EXP", "Gold", "RewardItem", "RecommendedLevel", "Tournament", "Notes"]
# Corrigidos à mão: mantidos numa regeneração
MANUAL_COLUMNS = ["Enemies"]


def snake(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower().replace("'", "")).strip("_")


def split_name(name):
    """("Viguro Lv2", "Master", 1) de "Viguro Lv2 (Master)"; ("Viguro", None, 2) de "Vigurox2"."""
    tournament = None
    m = re.search(r"\s*\((Expert|Master)\)$", name)
    if m:
        tournament, name = m.group(1), name[:m.start()]
    count = 1
    m = re.search(r"x(\d+)$", name)
    if m:
        count, name = int(m.group(1)), name[:m.start()]
    return name, tournament, count


def main():
    ap = argparse.ArgumentParser(prog="generate_bosses_csv")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    bosses = lt.load("bosses")["boss"]
    enemies, _ = lt.read_rows("DT_Enemies")
    items, _ = lt.read_rows("DT_Items")
    old_rows, old_order = lt.read_rows(TABLE)

    rows, report = [], {"sem inimigo no DT_Enemies": [], "item fora do DT_Items": [],
                        "Enemies mantido do CSV (posto à mão)": []}
    for b in bosses:
        key = snake(b["name"])
        base, tournament, count = split_name(b["name"])
        enemy = snake(base)                                  # "Songi (1st)" -> songi_1st
        found = enemy in enemies
        if not found:
            report["sem inimigo no DT_Enemies"].append("%s (procurado: %s)" % (b["name"], enemy))
        attacks = b.get("attacks", [])
        item = b.get("item_reward") or ""
        if item and item not in items:
            report["item fora do DT_Items"].append("%s: %s" % (key, item))
        rows.append({
            "---": key, "Key": key, "DisplayName": base if tournament or count > 1 else b["name"],
            "Location": b.get("location", ""), "FightCode": b.get("meth_id") or "",
            "Enemies": lt.ue_array([enemy] * count) if found else "()",
            "HP": b.get("hp_max") or 0, "MP": b.get("mp") or 0,
            "SpecialAttacks": lt.ue_array([a["name"] for a in attacks], quote=True),
            "SpecialAttackMP": lt.ue_array([a.get("mp") or 0 for a in attacks]),
            "EXP": b.get("exp_reward") or 0, "Gold": b.get("gold_reward") or 0, "RewardItem": item,
            "RecommendedLevel": b.get("recommended_level") or "", "Tournament": tournament or "",
            "Notes": b.get("notes", ""),
        })

    # Trabalho manual sobrevive à regeneração
    generated = {r["---"] for r in rows}
    for r in rows:
        old = old_rows.get(r["---"])
        for c in MANUAL_COLUMNS:
            if old and old.get(c, "") not in ("", "()") and old[c] != r[c]:
                r[c] = old[c]
                report["Enemies mantido do CSV (posto à mão)"].append(r["---"])
    custom = [old_rows[k] for k in old_order if k not in generated]
    rows += custom

    print("[bosses] %d linhas (%d do TOML, %d criadas fora do gerador)" % (len(rows), len(bosses), len(custom)))
    for title, lines in report.items():
        if lines:
            print("[bosses] %s: %d" % (title, len(lines)))
            for line in lines:
                print("            " + line)
    if args.dry_run:
        print("[bosses] --dry-run: nada escrito.")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[bosses] backup: " + bak)
    lt.write_rows(TABLE, COLUMNS, rows)
    print("[bosses] OK: " + lt.csv_path(TABLE))


if __name__ == "__main__":
    main()
