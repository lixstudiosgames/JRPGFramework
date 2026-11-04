#!/usr/bin/env python3
"""
generate_enemies_csv.py — gera Docs/Data/DT_Enemies.csv (Row Struct: FJRPGEnemyData).

    python Extras/generate_enemies_csv.py --monster-list <monster_list.txt> [--dry-run]
        [--archive <extracted/PROT/0867_battle_data.BIN> --tool <asset.exe>]

Duas fontes, cada uma com o que a outra não tem:
  - Docs/Data/gamedata/enemies.toml (do legend-of-legaia-re): nome, local, elemento, drop e
    steal por key do DT_Items, boss. Os stats dele são os de uma luta de boss da versão
    americana (registro do disco x fatores) — servem só para conferir o casamento.
  - A listagem do arquivo de monstros do disco (PROT 0867), feita com as ferramentas do RE:
        asset monster-archive extracted/PROT/0867_battle_data.BIN > monster_list.txt
    Traz o id do slot (que é onde está o modelo — MonsterArchiveID), a chance do drop, o Seru
    capturável e a chance de captura, e o registro cru: stats, EXP e Gold.

Com --archive e --tool (o arquivo de monstros e o asset.exe do RE), cada registro também é lido do
bloco decodificado do monstro (`asset monster-archive --id N --dump-block`, Extras/monster_block.py)
para a TargetClass — o byte +0x1E, a classe de altura que decide quais golpes alcançam o monstro
(0 todo golpe alcança; 2 voando: o golpe baixo erra; 3 baixo: o golpe alto erra; BATTLE_ROADMAP.md ›
5.0) — e para conferir o Element do TOML contra o +0x1D (o inimigo só do disco ganha o do registro).
Sem eles, a TargetClass que já está no CSV é mantida.

A tabela guarda o REGISTRO CRU do disco (stats, EXP, Gold), igual em todas as versões do
jogo. O que cada versão faz com ele — o boost dos stats ao carregar a luta, o corte de EXP e
de Gold no fim — é regra de código (UJRPGBattleRules + EJRPGRuleset), não dado: a versão
japonesa paga mais e não dá boost, e um número pré-multiplicado não volta ao cru exato.

Junta as duas pelo nome (o disco escreve "Gimard $2" onde o TOML diz "Gimard Lv2"). Nome com
várias formas (Songi, Tetsu, Gaza, Cort) desempata pelo HP, que é igual nas duas fontes.
Registros do disco idênticos byte a byte entre si (o Cort em 180/183..186) ficam no menor id.
Inimigo que só existe no disco (Comm, Evil Bat) vira linha com o registro do disco. Inimigo
do TOML sem registro no disco fica com os números do TOML (que NÃO são crus) e sai no
relatório. Tudo que não casar fica para resolver à mão.

Regenerar NÃO apaga trabalho manual: Mesh e Anims (preenchidos pela Fase 2), um
MonsterArchiveID posto à mão onde o gerador não achou, e linhas criadas no LegaiaStudio são
mantidos. O CSV anterior vai para Docs/Data/_backup/ antes de ser reescrito.

O CSV leva dados do disco (ids, chances), como o DT_StatGrowth já leva. Os .glb, o
extracted/ e a própria listagem ficam fora do repositório (ARCHITECTURE.md, regra 48).
"""

import argparse
import collections
import os
import re
import sys

import legaia_toml as lt
import monster_block as mb

TABLE = "DT_Enemies"

COLUMNS = [
    "Key", "DisplayName", "Location", "bIsBoss", "Element", "TargetClass",
    "BaseHP", "BaseMP", "BaseAGL", "BaseATK", "BaseUDF", "BaseLDF", "BaseSPD", "BaseINT",
    "EXP", "Gold", "DropItem", "DropChance", "StealItem", "StealChance",
    "CaptureSeru", "CatchRate", "MonsterArchiveID", "Mesh", "Anims",
]

# Preenchidos fora do gerador: nunca sobrescritos por uma regeneração.
MANUAL_COLUMNS = ["Mesh", "Anims"]

ELEMENTS = {
    None: "None", "fire": "Fire", "wind": "Wind", "thunder": "Thunder", "lightning": "Thunder",
    "water": "Water", "earth": "Earth", "light": "Light", "dark": "Dark", "evil": "Evil",
}

# O byte +0x1D do registro (0 earth, 1 water, 2 fire, 3 wind, 4 thunder, 5 light, 6 dark, 7 neutro)
# no EJRPGElement
RECORD_ELEMENTS = {0: "Earth", 1: "Water", 2: "Fire", 3: "Wind", 4: "Thunder", 5: "Light", 6: "Dark", 7: "None"}

# stats do registro, na ordem do arquivo: AGL, ATK, UDF, LDF, INT, SPD.
def raw_stats(rec):
    """O registro do disco como está: é o que vai para a tabela."""
    agl, atk, udf, ldf, intel, spd = rec["stats"]
    return {
        "BaseHP": rec["hp"], "BaseMP": rec["mp"], "BaseAGL": agl,
        "BaseATK": atk, "BaseUDF": udf, "BaseLDF": ldf, "BaseSPD": spd, "BaseINT": intel,
        "EXP": rec["exp"], "Gold": rec["gold"],
    }


# Registro cru do disco -> o que o enemies.toml traz (crates/gamedata/tests/enemy_stats_vs_disc.rs):
# a luta de boss americana, com EXP x3/4 e Gold x5/16 (com o Golden Book). Só para conferir.
def battle_stats(rec):
    agl, atk, udf, ldf, intel, spd = rec["stats"]
    return {
        "BaseHP": rec["hp"], "BaseMP": rec["mp"], "BaseAGL": agl,
        "BaseATK": (atk * 5 + 2) // 4, "BaseUDF": udf * 2, "BaseLDF": ldf * 2,
        "BaseSPD": spd, "BaseINT": (intel * 9 + 4) // 8,
        "EXP": (rec["exp"] * 3 + 2) // 4, "Gold": (rec["gold"] * 5 + 8) // 16,
    }


LIST_RE = re.compile(
    r"\s*id\s+(?P<id>\d+)\s+(?P<name>.+?)\s+HP\s+(?P<hp>\d+)\s+MP\s+(?P<mp>\d+)\s+"
    r"stats \[(?P<stats>[^\]]*)\]\s+magic\s+\d+\s+gold\s+(?P<gold>\d+)\s+exp\s+(?P<exp>\d+)\s+"
    r"drop\s+(?P<drop>\d+)@\s*(?P<dropc>\d+)%\s+rdf\s+\d+"
    r"(?:\s+seru 0x(?P<seru>[0-9A-Fa-f]+)@\s*(?P<catch>\d+)%)?")


def parse_monster_list(path):
    recs = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LIST_RE.match(line)
            if not m:
                continue
            recs.append({
                "id": int(m["id"]), "name": m["name"].strip(),
                "hp": int(m["hp"]), "mp": int(m["mp"]),
                "stats": [int(x) for x in m["stats"].split(",")],
                "gold": int(m["gold"]), "exp": int(m["exp"]),
                "drop": int(m["drop"]), "drop_chance": int(m["dropc"]),
                "seru": int(m["seru"], 16) if m["seru"] else 0,
                "catch": int(m["catch"]) if m["catch"] else 0,
            })
    return recs


def norm(name):
    """'Gimard Lv2' e 'Gimard $2' -> 'gimardlv2'."""
    return re.sub(r"[^a-z0-9]", "", name.lower().replace("$", "lv"))


def base_name(name):
    """Nome de exibição: sem a forma entre parênteses e sem o level ('Gimard Lv2' -> 'Gimard')."""
    name = name.split(" (")[0]
    return re.sub(r"\s+(Lv\s*\d+|\$\d+)$", "", name).strip()


def row_key(name):
    """'Gimard Lv2' -> gimard_lv2, 'Gaza (Sim-Seru)' -> gaza_sim_seru, 'Gimard $2' -> gimard_lv2."""
    s = name.lower().replace("$", "lv ")
    s = re.sub(r"lv\s+(\d)", r"lv\1", s)
    return re.sub(r"[^a-z0-9]+", "_", s).strip("_")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--monster-list", required=True,
                    help="saída de `asset monster-archive <0867_battle_data.BIN>`")
    ap.add_argument("--archive", help="extracted/PROT/0867_battle_data.BIN (para a TargetClass)")
    ap.add_argument("--tool", help="o asset.exe do RE (para a TargetClass)")
    ap.add_argument("--dry-run", action="store_true", help="só o relatório, sem escrever o CSV")
    args = ap.parse_args()
    if bool(args.archive) != bool(args.tool):
        sys.exit("--archive e --tool vão juntos.")

    toml = lt.load("enemies")["enemy"]
    recs = parse_monster_list(args.monster_list)
    if not recs:
        sys.exit("Nenhum registro na listagem — é a saída de `asset monster-archive` sem --id?")

    # Registros idênticos (mesmo nome e mesmos números) ficam no menor id.
    unique, duplicates = {}, collections.defaultdict(list)
    for r in sorted(recs, key=lambda r: r["id"]):
        sig = (r["name"], r["hp"], r["mp"], tuple(r["stats"]), r["gold"], r["exp"], r["drop"], r["seru"])
        if sig in unique:
            duplicates[unique[sig]["id"]].append(r["id"])
        else:
            unique[sig] = r
    disc = list(unique.values())
    rec_by_id = {r["id"]: r for r in recs}
    by_name = collections.defaultdict(list)
    for r in disc:
        by_name[norm(r["name"])].append(r)

    items, _ = lt.read_rows("DT_Items")
    seru_names = {s["name"] for s in lt.load("magic")["spell"]} if _has_toml("magic") else set()
    old_rows, old_order = lt.read_rows(TABLE)

    report = collections.defaultdict(list)
    used_ids = set()
    rows = []

    for e in toml:
        name = e["name"]
        cands = by_name.get(norm(name)) or by_name.get(norm(name.split(" (")[0]), [])
        rec = None
        if len(cands) == 1:
            rec = cands[0]
        elif len(cands) > 1:
            same_hp = [c for c in cands if c["hp"] == e["hp"]]
            if len(same_hp) == 1:
                rec = same_hp[0]
                report["desempate pelo HP"].append("%s -> id %d" % (name, rec["id"]))
            else:
                report["AMBÍGUO (resolver à mão)"].append(
                    "%s: ids %s" % (name, [c["id"] for c in cands]))
        else:
            report["sem registro no disco"].append(name)
        if not rec:
            # Um MonsterArchiveID posto à mão no CSV é o registro desta linha: os números saem dele
            # (crus), não do TOML — senão o EXP x3/4 do TOML seria cortado de novo pelas regras USA.
            old = old_rows.get(row_key(name))
            manual = str(old.get("MonsterArchiveID", "")) if old else ""
            if manual.lstrip("-").isdigit() and int(manual) in rec_by_id:
                rec = rec_by_id[int(manual)]
                report["MonsterArchiveID posto à mão: registro cru aplicado"].append(
                    "%s -> id %d" % (name, rec["id"]))

        row = {
            "---": row_key(name), "Key": row_key(name), "DisplayName": base_name(name),
            "Location": e.get("location", ""), "bIsBoss": lt.ue_bool(e.get("boss", False)),
            "Element": ELEMENTS.get(e.get("element"), "None"),
            "BaseHP": e["hp"], "BaseMP": e["mp"], "BaseAGL": e["agl"], "BaseATK": e["atk"],
            "BaseUDF": e["udf"], "BaseLDF": e["ldf"], "BaseSPD": e["spd"], "BaseINT": e["int"],
            "EXP": e["exp"], "Gold": e["gold"],
            "DropItem": e.get("drop") or "", "DropChance": 0,
            "StealItem": e.get("steal") or "", "StealChance": e.get("steal_chance", 0),
            "CaptureSeru": "", "CatchRate": 0, "MonsterArchiveID": -1,
        }
        if e.get("element") not in ELEMENTS:
            report["elemento desconhecido"].append("%s: %s" % (name, e.get("element")))
        if rec:
            used_ids.add(rec["id"])
            row["MonsterArchiveID"] = rec["id"]
            row["DropChance"] = rec["drop_chance"] if row["DropItem"] else 0
            if rec["seru"]:
                row["CaptureSeru"] = base_name(rec["name"])
                row["CatchRate"] = rec["catch"]
                if seru_names and row["CaptureSeru"] not in seru_names:
                    report["Seru fora do magic.toml"].append("%s: %s" % (name, row["CaptureSeru"]))
            # O TOML (de batalha) tem que bater com o disco convertido, a menos de 1 — é o que
            # prova que o casamento pelo nome pegou o registro certo.
            calc = battle_stats(rec)
            off = ["%s %s/%s" % (k, row[k], v) for k, v in calc.items() if abs(int(row[k]) - v) > 1]
            if off:
                report["TOML difere do disco (toml/disco)"].append("%s: %s" % (name, ", ".join(off)))
            # Na tabela vai o registro cru.
            row.update(raw_stats(rec))
        else:
            report["sem registro no disco: ficou com os números do TOML, que NÃO são crus"].append(name)
        rows.append(row)

    for r in disc:
        if r["id"] in used_ids:
            continue
        key = row_key(r["name"])
        if any(x["---"] == key for x in rows):
            key = "%s_%d" % (key, r["id"])
        row = {"---": key, "Key": key, "DisplayName": base_name(r["name"]), "Location": "",
               "bIsBoss": "False", "Element": "None", "DropItem": "", "DropChance": 0,
               "StealItem": "", "StealChance": 0, "CaptureSeru": "", "CatchRate": 0,
               "MonsterArchiveID": r["id"]}
        row.update(raw_stats(r))
        if r["seru"]:
            row["CaptureSeru"], row["CatchRate"] = base_name(r["name"]), r["catch"]
        note = "id %d %s" % (r["id"], r["name"])
        if r["drop"]:
            note += " (drop do disco: item #%d, sem key — preencher DropItem à mão)" % r["drop"]
        report["só no disco (Element e Location vazios)"].append(note)
        rows.append(row)

    for keep, dups in duplicates.items():
        report["registros idênticos no disco (mantido o menor id)"].append("%d = %s" % (keep, dups))

    # O registro do monstro: a classe de altura (+0x1E) e o elemento (+0x1D)
    if args.archive:
        with mb.BlockReader(args.tool, args.archive) as reader:
            for r in rows:
                monster_id = int(r["MonsterArchiveID"])
                if monster_id < 0:
                    continue
                block = reader.read(monster_id)
                r["TargetClass"] = block["target_class"]
                disc_element = RECORD_ELEMENTS.get(block["element"], "None")
                if r["Element"] == "None" and not r.get("Location"):
                    r["Element"] = disc_element        # só no disco: o registro é a fonte
                elif r["Element"] != disc_element:
                    report["Element do TOML difere do registro (toml/disco)"].append(
                        "%s: %s/%s" % (r["---"], r["Element"], disc_element))
    # Trabalho manual sobrevive à regeneração.
    generated = {r["---"] for r in rows}
    for r in rows:
        old = old_rows.get(r["---"])
        if not args.archive:
            r["TargetClass"] = old.get("TargetClass", 0) if old and old.get("TargetClass", "") != "" else 0
        for c in MANUAL_COLUMNS:
            r[c] = old.get(c, "") if old else ""
        if old and int(r["MonsterArchiveID"]) == -1 and old.get("MonsterArchiveID") not in ("", "-1"):
            r["MonsterArchiveID"] = old["MonsterArchiveID"]
            report["MonsterArchiveID mantido do CSV (posto à mão)"].append(r["---"])
        if not r["Anims"]:
            r["Anims"] = "()"
    for key in old_order:
        if key not in generated:
            rows.append(old_rows[key])
            report["linhas mantidas do CSV (criadas fora do gerador)"].append(key)

    for r in rows:
        for c in ("DropItem", "StealItem"):
            if r.get(c) and r[c] not in items:
                report["%s fora do DT_Items" % c].append("%s: %s" % (r["---"], r[c]))

    matched = sum(1 for r in rows if str(r.get("MonsterArchiveID", "-1")) not in ("", "-1"))
    print("[enemies] %d linhas (%d do TOML, %d com MonsterArchiveID); disco: %d registros, %d únicos"
          % (len(rows), len(toml), matched, len(recs), len(disc)))
    for title, lines in report.items():
        print("[enemies] %s: %d" % (title, len(lines)))
        for line in lines:
            print("            " + line)

    if args.dry_run:
        print("[enemies] --dry-run: nada escrito.")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[enemies] backup: " + bak)
    lt.write_rows(TABLE, COLUMNS, rows)
    print("[enemies] OK: " + lt.csv_path(TABLE))


def _has_toml(name):
    return os.path.isfile(os.path.join(lt.GAMEDATA_DIR, name + ".toml"))


if __name__ == "__main__":
    main()
