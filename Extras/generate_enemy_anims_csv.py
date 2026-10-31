#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_enemy_anims_csv.py — preenche Mesh e Anims do Docs/Data/DT_Enemies.csv a partir do lote
de monstros (o monsters.json do Extras/ps1_monsters_batch.py).

    python Extras/generate_enemy_anims_csv.py <monsters.json> [--dry-run]

Cada linha casa pelo MonsterArchiveID. Os papéis do FJRPGMonsterAnimSet (Battle/EnemyData.h) vêm
do id de cada ação no disco: 0 Idle, 1 Walk, 2 Flinch1, 3 Flinch2, 4 Knockdown, 5 GetUp, 7 Ready,
8 Recover, 9 Defeat, 0x0B Block. Attacks = as ações castáveis (0x0C..0x1F com custo de AGL, as que
a IA sorteia), na ordem do arquivo. As outras (0x23, as repetidas) ficam só como clip.

Malha e clips nos caminhos do ue_setup_monster.py: /Game/Legaia/Battle/Enemies/<Nome>/SK_<Nome> e
AS_<Nome>_<Clip>. As linhas sem id no manifest ficam como estão. O CSV anterior vai para
Docs/Data/_backup/.
"""

import argparse
import json
import sys

import legaia_toml as lt

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

TABLE = "DT_Enemies"
ROOT = "/Game/Legaia/Battle/Enemies"
ROLES = [(0x00, "Idle"), (0x01, "Walk"), (0x02, "Flinch1"), (0x03, "Flinch2"), (0x04, "Knockdown"),
         (0x05, "GetUp"), (0x07, "Ready"), (0x08, "Recover"), (0x09, "Defeat"), (0x0B, "Block")]


def asset(name, asset_name):
    return "%s/%s/%s.%s" % (ROOT, name, asset_name, asset_name)


def anim_set(info):
    name = info["name"]
    first = {}
    for c in info["clips"]:
        first.setdefault(c["action"], c["clip"])
    parts = ['%s="%s"' % (role, asset(name, "AS_%s_%s" % (name, first[act])))
             for act, role in ROLES if act in first]
    attacks = [asset(name, "AS_%s_%s" % (name, c["clip"])) for c in info["clips"] if c["attack"]]
    if attacks:
        parts.append("Attacks=(%s)" % ",".join('"%s"' % a for a in attacks))
    return "(%s)" % ",".join(parts)


def main():
    ap = argparse.ArgumentParser(prog="generate_enemy_anims_csv")
    ap.add_argument("monsters", help="monsters.json do Extras/ps1_monsters_batch.py")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    monsters = json.load(open(args.monsters, encoding="utf-8"))
    rows, order = lt.read_rows(TABLE)
    filled, changed, missing = 0, 0, []
    for key in order:
        r = rows[key]
        info = monsters.get(str(r.get("MonsterArchiveID", "")).strip())
        if not info:
            missing.append("%s (id %s)" % (key, r.get("MonsterArchiveID")))
            continue
        mesh, anims = asset(info["name"], "SK_%s" % info["name"]), anim_set(info)
        if (r["Mesh"], r["Anims"]) != (mesh, anims):
            changed += 1
        r["Mesh"], r["Anims"] = mesh, anims
        filled += 1
    print("[enemy-anims] %d linha(s) com malha, %d mudaram" % (filled, changed))
    if missing:
        print("[enemy-anims] sem modelo no lote: %s" % ", ".join(missing))
    if args.dry_run:
        print("[enemy-anims] --dry-run: nada escrito.")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[enemy-anims] backup: " + bak)
    columns = [c for c in next(iter(rows.values())) if c != "---"]
    lt.write_rows(TABLE, columns, [rows[k] for k in order])
    print("[enemy-anims] OK: " + lt.csv_path(TABLE))


if __name__ == "__main__":
    main()
