#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_arts_csv.py — gera Docs/Data/DT_Arts.csv (Row Struct: FJRPGArtData, Arts/ArtsData.h).

    python Extras/generate_arts_csv.py [--extracted <LegaiaRE/extracted> --tools <pasta do lzs-decode.exe>]
                                       [--reset] [--dry-run]

Fontes:
  - Docs/Data/gamedata/arts.toml (do legend-of-legaia-re): as 60 Arts — herói, nome, tipo, AP do
    original, botões e o Action Constant (o registro da Art no disco).
  - Com --extracted/--tools, o banco de Arts do disco (record[0] +0x58 dos arquivos do jogador,
    PROT 0863..0865; registros de 0xD0 indexados por Action Constant - 0x10): o poder de cada
    golpe, os bytes em +0x24 (RE: docs/formats/art-data.md, "Damage power byte").
  - As sequências das Super Arts e das Miracle Arts, que o RE capturou na RAM da batalha
    (crates/art/src/super_art.rs, o "replace" de cada uma; miracle.rs, o "replacement"). Estão
    aqui como dados: SUPERS e MIRACLES.

Linhas:
  - Uma por Art do TOML. Super e Miracle são a receita (Command + Sequence); quem tem registro
    no disco são as peças.
  - Uma "Part" por registro que só é tocado dentro de uma sequência: o finalizador de cada Super,
    as peças da Miracle que não são Arts digitáveis, os segmentos seguintes de uma Art que o disco
    divide em vários registros (o Hurricane Kick da Noa: mesmo combo em 0x1C..0x1E).

Poder: byte 0x16..0x1A = UDF x 12/18/20/22/28, 0x1B..0x1F = LDF, 0x0C..0x15 = o alcance
alternativo (o alto passa por cima de inimigo baixo, o baixo por baixo de voador).

AP (a inversão do remake, ver BATTLE_ROADMAP.md › Phase 6): Regular -> APGain = ap / 2; Hyper,
Super, Miracle -> APCost = ap. CommandCost = número de botões.

Regenerar NÃO apaga trabalho manual: linhas que o gerador não cria (Arts novas) ficam como
estão; nas linhas geradas, o balanceamento (APGain, APCost, CommandCost, Anim, HitPower,
HitTarget, Sequence) já editado é mantido — --reset refaz tudo do original. O CSV anterior vai
para Docs/Data/_backup/.
"""

import argparse
import os
import re
import struct
import sys

import legaia_toml as lt
from ps1_party_sounds import ART_NAMES, CHARACTERS, PLAYER_FILES, camel, lzs

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

TABLE = "DT_Arts"
COLUMNS = ["Key", "Character", "DisplayName", "Kind", "Command", "OriginalAP", "APGain", "APCost",
           "CommandCost", "Anim", "HitPower", "HitTarget", "Sequence", "ActionConstant"]
# Balanceamento: editado à mão, mantido numa regeneração (menos com --reset)
TUNED_COLUMNS = ["APGain", "APCost", "CommandCost", "Anim", "HitPower", "HitTarget", "Sequence"]

KINDS = {"regular": "Regular", "hyper": "Hyper", "super": "Super", "miracle": "Miracle"}
BUTTONS = {"Arms": "Arms", "Ra-Seru": "RaSeru", "High": "High", "Low": "Low"}
ANIM = "/Game/Legaia/Battle/Party/{c}/AS_{c}_Battle_{clip}.AS_{c}_Battle_{clip}"
ART_BANK, ART_RECORD, ART_BASE, FIRST_ART = 0x58, 0xD0, 0x10, 0x1B
MULTIPLIERS = [12, 18, 20, 22, 28]
# Passos do queue que não são Arts: os golpes de direção e os starters (os clips básicos do herói)
BASIC_STEPS = {0x0C: "SwingL", 0x0D: "SwingR", 0x0E: "SwingD", 0x0F: "SwingU",
               0x19: "ArtStarter", 0x1A: "SpecialStarter"}

# O "replace" de cada Super Art (legend-of-legaia-re, crates/art/src/super_art.rs; capturado da
# tabela de gatilhos da batalha, 0x801F65E8): as Arts que o jogador fez, o conector entre elas e,
# depois do 0x1A, o finalizador no lugar da última.
SUPERS = {
    ("Vahn", "Tri-Somersault"): [0x19, 0x27, 0x0F, 0x19, 0x1F, 0x0E, 0x1A, 0x2B, 0x2B, 0x2B],
    ("Vahn", "Maximum Blow"): [0x19, 0x28, 0x0E, 0x19, 0x26, 0x0C, 0x1A, 0x2C],
    ("Vahn", "Fire Tackle"): [0x19, 0x29, 0x0C, 0x19, 0x25, 0x0D, 0x1A, 0x2D],
    ("Vahn", "Power Slash"): [0x19, 0x28, 0x0E, 0x19, 0x27, 0x0E, 0x1A, 0x2E],
    ("Vahn", "Rolling Combo"): [0x19, 0x22, 0x0C, 0x19, 0x25, 0x0F, 0x0F, 0x1A, 0x2F, 0x30],
    ("Noa", "Triple Lizard"): [0x19, 0x25, 0x0F, 0x19, 0x24, 0x0E, 0x1A, 0x2E, 0x2E, 0x2E],
    ("Noa", "Super Javelin"): [0x19, 0x22, 0x0E, 0x1A, 0x2F],
    ("Noa", "Super Tempest"): [0x19, 0x26, 0x0D, 0x0C, 0x0F, 0x0F, 0x1A, 0x30],
    ("Noa", "Love You"): [0x19, 0x27, 0x0E, 0x19, 0x2B, 0x0E, 0x0C, 0x1A, 0x31],
    ("Noa", "Dragon Fangs"): [0x19, 0x2B, 0x0F, 0x19, 0x24, 0x0E, 0x1A, 0x32],
    ("Gala", "Back Punch x3"): [0x19, 0x27, 0x0F, 0x19, 0x29, 0x0D, 0x1A, 0x2B, 0x2B, 0x2B],
    ("Gala", "Super Ironhead"): [0x19, 0x29, 0x0F, 0x19, 0x24, 0x0E, 0x1A, 0x2C],
    ("Gala", "Rushing Crush"): [0x19, 0x28, 0x0F, 0x19, 0x29, 0x0F, 0x1A, 0x2D],
    ("Gala", "Heaven's Drop"): [0x19, 0x29, 0x0F, 0x19, 0x24, 0x0C, 0x0E, 0x1A, 0x2E],
    ("Gala", "Neo Static Raising"): [0x19, 0x26, 0x0F, 0x19, 0x25, 0x0C, 0x0D, 0x0F, 0x1A, 0x2F],
}
# O "replacement" de cada Miracle Art (crates/art/src/miracle.rs): o queue inteiro vira isto.
MIRACLES = {
    "Vahn": [0x0C, 0x0D, 0x0E, 0x0F, 0x1A, 0x22, 0x28, 0x23, 0x27, 0x20, 0x2A],
    "Noa": [0x0D, 0x0C, 0x0F, 0x0E, 0x1A, 0x2A, 0x26, 0x27, 0x2B, 0x24, 0x2C, 0x2D],
    "Gala": [0x0C, 0x0D, 0x0E, 0x0F, 0x1A, 0x26, 0x23, 0x27, 0x20, 0x28, 0x2A],
}


def snake(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower().replace("'", "")).strip("_")


def decode_power(byte):
    """(multiplicador, alvo) ou None (byte sem dano)."""
    if 0x16 <= byte <= 0x1F:
        i = byte - 0x16
        return MULTIPLIERS[i % 5], "Upper" if i < 5 else "Lower"
    if 0x0C <= byte <= 0x15:
        i = byte - 0x0C
        return MULTIPLIERS[i % 5], "UpperMissesShort" if i < 5 else "LowerMissesFloating"
    return None


def read_banks(extracted, tools):
    """{herói: {constante: {"name", "combo", "hits"}}} do banco de Arts de cada arquivo do jogador."""
    prot = os.path.join(extracted, "PROT")
    banks = {}
    for char in CHARACTERS:
        path = os.path.join(prot, next(f for f in os.listdir(prot) if f.startswith(PLAYER_FILES[char] + "_")))
        budget = struct.unpack_from("<I", open(path, "rb").read(16), 0xC)[0]
        record0 = lzs(tools, path, 0x10, budget)
        bank = struct.unpack_from("<I", record0, ART_BANK)[0]
        records = {}
        for k in range(struct.unpack_from("<I", record0, bank)[0]):
            const = ART_BASE + k
            if const < FIRST_ART:
                continue
            rec = bank + 4 + ART_RECORD * k
            hits = []
            for b in record0[rec + 0x24:rec + 0x28]:
                hit = decode_power(b)
                if not hit:
                    break
                hits.append(hit)
            records[const] = {"name": record0[rec + 0x10:rec + 0x24].split(b"\0")[0].decode("ascii", "replace"),
                              "combo": bytes(record0[rec:rec + 0x10].split(b"\0")[0]), "hits": hits}
        banks[char] = records
    return banks


def chain_of(records, first):
    """O registro e os seguintes com o mesmo nome ou o mesmo combo (2+ direções): uma Art só que o
    disco divide em segmentos (RE: crates/art/src/clip_ladder.rs, chain_of)."""
    chain, base = [first], records.get(first)
    c = first + 1
    while base and c in records:
        same_name = base["name"] and records[c]["name"] == base["name"]
        same_combo = len(base["combo"]) >= 2 and records[c]["combo"] == base["combo"]
        if not (same_name or same_combo):
            break
        chain.append(c)
        c += 1
    return chain


def build(arts, banks):
    rows, by_const, notes = [], {}, []

    def art_name(char, const):
        names = ART_NAMES[char]
        return names[const - FIRST_ART] if 0 <= const - FIRST_ART < len(names) else "Art 0x%02X" % const

    def own_record(row, char, const):
        row["ActionConstant"] = const
        row["Anim"] = ANIM.format(c=char, clip=camel(art_name(char, const)))
        hits = banks[char].get(const, {}).get("hits", []) if banks else []
        row["HitPower"] = lt.ue_array([h[0] for h in hits])
        row["HitTarget"] = lt.ue_array([h[1] for h in hits])

    for a in arts:
        char, kind = a["character"], KINDS[a["kind"]]
        key = "%s_%s" % (char.lower(), snake(a["name"]))
        command = [BUTTONS[b] for b in a["command"]]
        row = {"---": key, "Key": key, "Character": char, "DisplayName": a["name"], "Kind": kind,
               "Command": lt.ue_array(command), "OriginalAP": a["ap"],
               "APGain": a["ap"] // 2 if kind == "Regular" else 0, "APCost": 0 if kind == "Regular" else a["ap"],
               "CommandCost": len(command), "Anim": "", "HitPower": "()", "HitTarget": "()",
               "Sequence": "()", "ActionConstant": 0}
        if kind != "Super":                       # a Super não tem registro próprio: é a receita
            own_record(row, char, a["action_constant"])
            by_const[(char, a["action_constant"])] = key
        rows.append(row)

    parts = {}

    def step(char, const):
        if const in BASIC_STEPS:
            return BASIC_STEPS[const]
        if (char, const) in by_const:
            return by_const[(char, const)]
        if (char, const) not in parts:
            name = art_name(char, const)
            key = "%s_%s" % (char.lower(), snake(name))
            if any(r["---"] == key for r in rows):
                key, name = key + "_finisher", name + " (finisher)"
            part = {"---": key, "Key": key, "Character": char, "DisplayName": name, "Kind": "Part",
                    "Command": "()", "OriginalAP": 0, "APGain": 0, "APCost": 0, "CommandCost": 0,
                    "Sequence": "()"}
            own_record(part, char, const)
            parts[(char, const)] = part
        return parts[(char, const)]["---"]

    for row in rows:
        char = row["Character"]
        if row["Kind"] == "Super":
            seq = SUPERS.get((char, row["DisplayName"]))
            if seq is None:
                notes.append("Super sem sequência conhecida: %s" % row["---"])
                continue
            row["Sequence"] = lt.ue_array([step(char, c) for c in seq])
        elif row["Kind"] == "Miracle":
            row["Sequence"] = lt.ue_array([step(char, c) for c in MIRACLES[char]])
        elif banks:
            chain = chain_of(banks[char], row["ActionConstant"])
            if len(chain) > 1:
                row["Sequence"] = lt.ue_array([row["---"]] + [step(char, c) for c in chain[1:]])
    return rows + list(parts.values()), notes


def main():
    ap = argparse.ArgumentParser(prog="generate_arts_csv")
    ap.add_argument("--extracted", help="a pasta extracted/ do legaia-extract (poder dos golpes)")
    ap.add_argument("--tools", help="a pasta das ferramentas do RE (lzs-decode.exe)")
    ap.add_argument("--reset", action="store_true", help="refaz também o balanceamento já editado")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    banks = read_banks(args.extracted, args.tools) if args.extracted and args.tools else None
    if not banks:
        print("[arts] sem --extracted/--tools: HitPower/HitTarget ficam vazios nas linhas novas")
    rows, notes = build(lt.load("arts")["arts"], banks)

    old_rows, old_order = lt.read_rows(TABLE)
    generated = {r["---"] for r in rows}
    kept = 0
    for r in rows:
        old = old_rows.get(r["---"])
        if old and not args.reset:
            for c in TUNED_COLUMNS:
                if old.get(c, "") not in ("", "()") or not banks and c in ("HitPower", "HitTarget"):
                    kept += old[c] != str(r[c])
                    r[c] = old[c]
    custom = [old_rows[k] for k in old_order if k not in generated]
    rows += custom

    kinds = {}
    for r in rows:
        kinds[r["Kind"]] = kinds.get(r["Kind"], 0) + 1
    print("[arts] %d linhas: %s; %d valor(es) de balanceamento mantido(s); %d Art(s) nova(s) mantida(s)"
          % (len(rows), ", ".join("%d %s" % (n, k) for k, n in kinds.items()), kept, len(custom)))
    for n in notes:
        print("[arts] " + n)
    if args.dry_run:
        print("[arts] --dry-run: nada escrito.")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[arts] backup: " + bak)
    lt.write_rows(TABLE, COLUMNS, rows)
    print("[arts] OK: " + lt.csv_path(TABLE))


if __name__ == "__main__":
    main()
