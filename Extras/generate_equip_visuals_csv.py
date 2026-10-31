#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_equip_visuals_csv.py — gera/atualiza Docs/Data/DT_EquipVisuals.csv a partir das peças de
equipamento cortadas pelo Extras/ps1_equip_pieces.py (o pieces.json de cada personagem).

    python Extras/generate_equip_visuals_csv.py <pieces/<nome>> [<pieces/<outro>> ...]

Cada linha diz qual malha veste uma parte do herói na luta (FJRPGEquipVisualRow,
Battle/BattleTypes.h). O row name é a chave que a arena procura:

    <Personagem>_<Parte>_<Item>   ex.: Vahn_Weapon_survival_knife, Vahn_RaSeru_Lv3, Vahn_Body_Default

Item é a chave da DT_Items, Lv<N> para o Ra-Seru no nível N, ou Default para a parte sem
equipamento. As linhas dos personagens passados são refeitas; as dos outros ficam como estão.
As malhas ficam em /Game/Legaia/Battle/Party/<Nome>/Pieces/SK_<peça> (importadas com o
esqueleto do SK_<Nome>_Battle — Docs/Guides/PS1_ASSETS.md, "Equipment pieces").
"""

import csv
import json
import os
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(PLUGIN_DIR, "Docs", "Data", "DT_EquipVisuals.csv")

# Colunas = UPROPERTY de FJRPGEquipVisualRow, na ordem do header C++
COLUMNS = ["Character", "Piece", "Item", "Mesh"]

MESH_PATH = "/Game/Legaia/Battle/Party/{char}/Pieces/SK_{piece}.SK_{piece}"


def rows_for(pieces_dir):
    """(personagem, {row name: linha}) das peças de uma pasta."""
    pieces = json.load(open(os.path.join(pieces_dir, "pieces.json"), encoding="utf-8"))
    rows, char = {}, None
    for p in pieces:
        char, slot, rest = p["piece"].split("_", 2)
        item = p["item"] or rest            # chave da DT_Items, ou Default / Lv<N>
        name = "%s_%s_%s" % (char, slot, item)
        rows[name] = {"Character": char, "Piece": slot, "Item": item,
                      "Mesh": MESH_PATH.format(char=char, piece=p["piece"])}
    return char, rows


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    existing = {}
    if os.path.exists(CSV_PATH):
        with open(CSV_PATH, encoding="utf-8", newline="") as f:
            existing = {r["---"]: r for r in csv.DictReader(f)}

    for pieces_dir in argv:
        char, rows = rows_for(os.path.abspath(pieces_dir))
        existing = {k: v for k, v in existing.items() if v.get("Character") != char}
        existing.update(rows)
        print("%s: %d linhas" % (char, len(rows)))

    with open(CSV_PATH, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["---"] + COLUMNS)
        for name in sorted(existing):
            w.writerow([name] + [existing[name].get(c, "") for c in COLUMNS])
    print("-> %s (%d linhas)" % (CSV_PATH, len(existing)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
