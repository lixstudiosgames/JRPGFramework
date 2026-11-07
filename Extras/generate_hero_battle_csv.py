#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_hero_battle_csv.py — gera as tabelas pequenas que a luta lê do disco para os heróis:

  Docs/Data/DT_RaSeruTiers.csv  (Row Struct: FJRPGRaSeruTierData)  o ATK do Ra-Seru de cada herói, por tier
  Docs/Data/DT_WeaponCosts.csv  (Row Struct: FJRPGWeaponCostData)  o custo do botão de Arms de cada arma
  Docs/Data/DT_HeroSwings.csv   (Row Struct: FJRPGHeroSwingData)    o golpe simples de cada botão, por herói e equipamento:
                                                                    byte(s) de poder, quadro(s) de golpe, rate e custo
  Docs/Data/DT_HeroReactions.csv (Row Struct: FJRPGHeroReactionData) quanto duram as reações de cada herói (flinch, block,
                                                                    knockdown, levantar, a queda), em quadros lógicos
  Docs/Data/DT_ArtHits.csv      (Row Struct: FJRPGArtHitData)       o quadro do clip de cada golpe das Arts (e o rate)

    python Extras/generate_hero_battle_csv.py --extracted <LegaiaRE/extracted> --tool <asset.exe> [--lzs-tool <lzs-decode.exe>] [--dry-run]

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

O DT_HeroSwings lê o MESMO registro de golpe que o custo (o registro que o item equipado traz para o arquivo de batalha):
`[+0x00..+0x03]` os bytes de poder (um por golpe), `[+0x10..+0x13]` os quadros de golpe (lista terminada em zero; a mesma
quantidade dos bytes de poder, 1 ou 2 no disco), `[+0x78]` o rate do clip e `[+0x74]` o custo do botão. Uma linha por
herói + item + botão: a arma dirige o Arms (na Noa, canhota, é a seção 3), o tier do Ra-Seru o Ra-Seru (na Noa é a seção
2), o calçado os dois chutes (o 1º registro é o Low, o 2º o High). Item = a key do DT_Items; `default` = a seção padrão
(nada equipado, id 0); `seru_N` = o tier N do Ra-Seru (o Ra-Seru aqui é o SeruLevel, não um item). Sai a mesma tabela que
o RE documenta (docs/reference/glitches/turn-order-and-arts.md, os calçados do Gala).

O DT_HeroReactions lê a `record[0]` de cada arquivo de batalha (PROT 0863 Vahn, 0864 Noa, 0865 Gala, 0866 Terra), o bloco LZS em
`+0x10` com `+0x0C` bytes decodificados: 12 `u32` no começo são os offsets das entradas de ação (a tag é o índice), e cada entrada
traz o rate em `+0x78` e, em `+0xAC`, `[u8 partes][u8 quadros]` do clip. A duração em quadros lógicos (vsyncs, a escala 8) é
`ceil(4 * quadros / rate)` (Docs/Reference/BATTLE_TIMING.md, seção 1). As entradas: 2 / 3 flinch, 4 knockdown, 5 levantar, 7 morrer,
8 o caído (repete para sempre), 0x0B block. Uma entrada de stream vazio (a Terra não tem a 7 e a 8) sai 0: a luta usa o padrão.

O DT_ArtHits lê o bloco das Arts da mesma record[0]: o ponteiro em `+0x58` mais 4 é o registro da constante 0x10, e cada
constante tem 0xD0 bytes (docs/formats/art-data.md). O registro é uma entrada de clip com 0x24 bytes na frente: o combo em
`+0x00`, os bytes de poder em `+0x24`, os quadros de golpe em `+0x34` (um por byte de poder; os cues de impacto 0x4C de `+0x78`
caem nos mesmos quadros) e o rate em `+0x9C`. Uma linha por Art do DT_Arts com ActionConstant; a Miracle (0x1B) não tem clip ali.

Fontes (do disco do usuário, lidas com as ferramentas do RE; nada além dos CSVs entra no repositório — regra 48):
  - `asset item-tables <SCUS_942.54> --equipment-only`
  - `asset battle-data-pack <PROT 0863|0864|0865> --out <pasta temporária>`
  - `lzs-decode raw <PROT 0863..0866> --skip 16 --size <budget> -o <arquivo temporário>` (a record[0], no DT_HeroReactions)
"""

import argparse
import csv
import glob
import io
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
SWINGS_TABLE = "DT_HeroSwings"
REACTIONS_TABLE = "DT_HeroReactions"

# A seção do arquivo de batalha (0 corpo, 1 cabeça, 2, 3, 4 calçado) de cada botão, por herói. O registro de golpe da
# seção 4 vem em dois: [+0x04] o chute baixo (slot 0xE) e [+0x08] o alto (0xF). A Noa tem as seções 2 e 3 trocadas
SWING_SECTIONS = {
    "Vahn": {2: ("Arms", "weapon"), 3: ("RaSeru", "seru")},
    "Gala": {2: ("Arms", "weapon"), 3: ("RaSeru", "seru")},
    "Noa":  {2: ("RaSeru", "seru"), 3: ("Arms", "weapon")},
}

# O Ra-Seru de cada herói e o arquivo de batalha dele
RASERU = {"Meta": "Vahn", "Terra": "Noa", "Ozma": "Gala"}
PACKS = {"Vahn": "0863", "Noa": "0864", "Gala": "0865"}
# As reações são de todos os quatro (a Terra, o lobo, tem arquivo próprio)
REACTION_PACKS = {"Vahn": "0863", "Noa": "0864", "Gala": "0865", "Terra": "0866"}
# A tag de cada entrada de reação da record[0] e a coluna do DT_HeroReactions
REACTION_ENTRIES = (("FlinchHighFrames", 2), ("FlinchLowFrames", 3), ("BlockFrames", 0x0B), ("KnockdownFrames", 4),
                    ("GetUpFrames", 5), ("DyingFrames", 7), ("DownLoopFrames", 8))
# O movimento de cada reação: a velocidade de raiz (+0x0C, unidades por vsync ao longo da direção; negativa = recua durante o
# clip inteiro) e o passo do fim do clip (+0x0E, o quanto o clip deixa o corpo de onde começou). A entrada 8 (o caído) é 0 em todo
# arquivo e fica de fora
REACTION_MOTION = (("FlinchHigh", 2), ("FlinchLow", 3), ("Block", 0x0B), ("Knockdown", 4), ("GetUp", 5), ("Dying", 7))

ART_HITS_TABLE = "DT_ArtHits"
# O bloco das Arts na record[0]: o ponteiro em +0x58 aponta 4 bytes antes do primeiro registro (o da constante 0x10)
ART_BLOCK_POINTER = 0x58
ART_BLOCK_SKIP = 4
ART_RECORD_SIZE = 0xD0

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


def read_swing(data, offset):
    """O golpe de um registro de golpe: {powers, beats, rate, cost}, ou None se o registro não cabe no slot."""
    if not offset or offset + 0x79 > len(data):
        return None
    beats = []
    for b in data[offset + 0x10:offset + 0x14]:
        if b == 0:
            break
        beats.append(b)
    powers = list(data[offset:offset + len(beats)])
    return {"powers": powers, "beats": beats, "rate": data[offset + 0x78], "cost": data[offset + 0x74],
            "root_speed": struct.unpack_from("<h", data, offset + 0x0C)[0],
            "end_step": struct.unpack_from("<h", data, offset + 0x0E)[0]}


def hero_swings(tool, extracted, character, workdir, equipment):
    """Os registros de golpe do arquivo de batalha de um herói: lista de (id, seção, botão, golpe), id 0 = o padrão."""
    matches = glob.glob(os.path.join(extracted, "PROT", PACKS[character] + "_*.BIN"))
    if not matches:
        sys.exit("Não achei PROT/%s_*.BIN em %s" % (PACKS[character], extracted))
    out_dir = os.path.join(workdir, character + "_swings")
    os.makedirs(out_dir, exist_ok=True)
    run(tool, "battle-data-pack", matches[0], "--out", out_dir)
    records = []
    for name in sorted(os.listdir(out_dir)):
        m = REC_RE.match(name)
        if m:
            records.append((int(m["n"]), int(m["id"], 16), os.path.join(out_dir, name)))
    out = []
    section = 0
    for _n, item_id, path in sorted(records):
        if section >= 2:
            with open(path, "rb") as f:
                data = f.read()
            first = struct.unpack_from("<I", data, 4)[0]
            if section == 4:
                second = struct.unpack_from("<I", data, 8)[0]
                for button, offset in (("Low", first), ("High", second)):
                    swing = read_swing(data, offset)
                    if swing:
                        out.append((item_id, section, button, swing))
            elif section in SWING_SECTIONS[character]:
                swing = read_swing(data, first)
                if swing:
                    out.append((item_id, section, SWING_SECTIONS[character][section][0], swing))
        if item_id == 0:
            section += 1     # o id 0 fecha a seção: é o padrão dela
    return out


def decode_record0(lzs_tool, extracted, character, workdir):
    """Os bytes da record[0] (LZS em +0x10, +0x0C bytes decodificados) do arquivo de batalha do herói."""
    matches = glob.glob(os.path.join(extracted, "PROT", REACTION_PACKS[character] + "_*.BIN"))
    if not matches:
        sys.exit("Não achei PROT/%s_*.BIN em %s" % (REACTION_PACKS[character], extracted))
    with open(matches[0], "rb") as f:
        budget = struct.unpack_from("<I", f.read(16), 12)[0]
    out = os.path.join(workdir, character + "_record0.bin")
    run(lzs_tool, "raw", matches[0], "--skip", "16", "--size", str(budget), "-o", out)
    with open(out, "rb") as f:
        return f.read()


def art_hits(data, character, arts, report):
    """As linhas do DT_ArtHits de um herói: o registro de cada Art com constante (bloco em [+0x58] + 4, 0xD0 por constante
    a partir de 0x10) traz os bytes de poder em +0x24, os quadros de golpe em +0x34 e o rate em +0x9C."""
    base = struct.unpack_from("<I", data, ART_BLOCK_POINTER)[0] + ART_BLOCK_SKIP
    rows = []
    for key, row in arts:
        if row.get("Character") != character:
            continue
        constant = int(row.get("ActionConstant") or 0)
        if constant < 0x10:
            continue   # Super Art (constante 0 no DT_Arts) e o que não tem registro próprio: fica no padrão
        off = base + (constant - 0x10) * ART_RECORD_SIZE
        if off + ART_RECORD_SIZE > len(data):
            report["registro de Art fora da record[0]"].append("%s %s (0x%02X)" % (character, key, constant))
            continue
        powers = []
        for b in data[off + 0x24:off + 0x28]:
            if b == 0:
                break
            powers.append(b)
        beats = list(data[off + 0x34:off + 0x34 + len(powers)])
        rate = data[off + 0x9C]
        expected = len(re.findall(r"-?\d+", row.get("HitPower", "")))
        if not powers or 0 in beats or not rate:
            report["registro de Art sem poder/quadro/rate"].append("%s %s (0x%02X)" % (character, key, constant))
            continue
        if expected and expected != len(beats):
            report["Art com golpes diferentes do DT_Arts"].append("%s %s: disco %d, DT_Arts %d" % (
                character, key, len(beats), expected))
        # A entrada do clip começa em +0x24 do registro: a velocidade de raiz em +0x30 e o passo do fim em +0x32
        rows.append({"---": key, "Character": character, "ActionConstant": constant,
                     "HitBeats": lt.ue_array(beats), "Rate": rate,
                     "RootSpeed": struct.unpack_from("<h", data, off + 0x30)[0],
                     "EndStep": struct.unpack_from("<h", data, off + 0x32)[0]})
    return rows


def reaction_lengths(lzs_tool, extracted, character, workdir):
    """{coluna: quadros lógicos} das entradas de reação da record[0] do arquivo de batalha do herói (0 = stream vazio)."""
    data = decode_record0(lzs_tool, extracted, character, workdir)
    offsets = struct.unpack_from("<12I", data, 0)
    lengths = {}
    for column, tag in REACTION_ENTRIES:
        offset = offsets[tag]
        if offset == 0 or offset + 0xAE > len(data) or data[offset] != tag:
            sys.exit("%s: a entrada %d da record[0] não é a esperada (tag %d no offset 0x%X)" % (
                character, tag, data[offset] if offset < len(data) else -1, offset))
        rate = data[offset + 0x78]
        frames = data[offset + 0xAD]
        # O clip de N quadros no rate R dura 4 * N / R vsyncs; o stream vazio (partes 0) não dura nada
        lengths[column] = -(-4 * frames // rate) if rate and data[offset + 0xAC] else 0
    for name, tag in REACTION_MOTION:
        offset = offsets[tag]
        lengths[name + "RootSpeed"] = struct.unpack_from("<h", data, offset + 0x0C)[0]
        lengths[name + "EndStep"] = struct.unpack_from("<h", data, offset + 0x0E)[0]
    return lengths


def same_as_csv(table, columns, rows):
    """O CSV que existe já é exatamente o que seria escrito? (para não gerar backup à toa)"""
    path = lt.csv_path(table)
    if not os.path.isfile(path):
        return False
    buf = io.StringIO(newline="")
    w = csv.writer(buf, lineterminator="\r\n")
    w.writerow(["---"] + columns)
    for r in rows:
        w.writerow([r["---"]] + [r.get(c, "") for c in columns])
    with open(path, "rb") as f:
        return f.read() == buf.getvalue().encode("utf-8")


def main():
    ap = argparse.ArgumentParser(prog="generate_hero_battle_csv")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract (PROT/, SCUS_942.54)")
    ap.add_argument("--tool", required=True, help="o asset.exe do RE (v1.10.0 ou mais novo)")
    ap.add_argument("--lzs-tool", help="o lzs-decode.exe do RE (padrão: o que está na pasta do --tool)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    scus = os.path.join(args.extracted, "SCUS_942.54")
    equipment = parse_equipment(args.tool, scus)
    if not equipment:
        sys.exit("`asset item-tables --equipment-only` não devolveu nada — o SCUS_942.54 é o da versão USA?")
    items, item_order = lt.read_rows("DT_Items")
    by_name = {row["Name"].strip().lower(): key for key, row in items.items()}
    report = {"arma sem key no DT_Items": [], "tier fora do padrão": [], "golpe sem key no DT_Items": [],
              "registro de golpe sem poder/quadro": []}

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

    # --- DT_HeroSwings ------------------------------------------------------------------------------
    swing_rows = []
    with tempfile.TemporaryDirectory(prefix="legaia_swings_") as workdir:
        for character in ("Vahn", "Noa", "Gala"):
            for item_id, section, button, swing in hero_swings(args.tool, args.extracted, character, workdir, equipment):
                if not swing["powers"]:
                    report["registro de golpe sem poder/quadro"].append("%s 0x%02X %s" % (character, item_id, button))
                    continue
                if item_id == 0:
                    item = "default"
                else:
                    name = equipment.get(item_id, ("?", 0))[0]
                    tier = TIER_RE.match(name)
                    if SWING_SECTIONS[character].get(section, ("", ""))[1] == "seru":
                        if not tier or RASERU[tier["seru"]] != character:
                            report["golpe sem key no DT_Items"].append("%s: Ra-Seru 0x%02X %s" % (character, item_id, name))
                            continue
                        item = "seru_%d" % int(tier["tier"])
                    else:
                        item = by_name.get(name.lower())
                        if not item:
                            report["golpe sem key no DT_Items"].append("%s: %s (0x%02X)" % (character, name, item_id))
                            continue
                        row = items[item]
                        # Arma: EquipBest / EquipOthers; calçado (armadura): EquipCharacter
                        owners = [row.get("EquipBest", ""), row.get("EquipCharacter", "")] + re.findall(r'"([^"]+)"', row.get("EquipOthers", ""))
                        if character not in owners:
                            continue   # a seção existe no arquivo mas o herói não equipa o item
                swing_rows.append({"---": "%s_%s_%s" % (character, item, button), "Character": character, "Item": item,
                                   "Button": button, "PowerBytes": lt.ue_array(swing["powers"]),
                                   "HitBeats": lt.ue_array(swing["beats"]), "Rate": swing["rate"], "Cost": swing["cost"],
                                   "RootSpeed": swing["root_speed"], "EndStep": swing["end_step"],
                                   "_default": item == "default", "_id": item_id})
    swing_rows.sort(key=lambda r: (r["Character"], not r["_default"], r["Button"], r["_id"]))

    # --- DT_HeroReactions ---------------------------------------------------------------------------
    lzs_tool = args.lzs_tool or os.path.join(os.path.dirname(os.path.abspath(args.tool)), "lzs-decode.exe")
    reaction_rows = []
    with tempfile.TemporaryDirectory(prefix="legaia_reactions_") as workdir:
        for character in ("Vahn", "Noa", "Gala", "Terra"):
            lengths = reaction_lengths(lzs_tool, args.extracted, character, workdir)
            row = {"---": character, "Character": character}
            row.update(lengths)
            reaction_rows.append(row)
    reaction_columns = (["Character"] + [column for column, _tag in REACTION_ENTRIES]
                        + [name + suffix for name, _tag in REACTION_MOTION for suffix in ("RootSpeed", "EndStep")])

    # --- DT_ArtHits ---------------------------------------------------------------------------------
    arts, arts_order = lt.read_rows("DT_Arts")
    arts_list = [(key, arts[key]) for key in arts_order]
    report.update({"registro de Art fora da record[0]": [], "registro de Art sem poder/quadro/rate": [],
                   "Art com golpes diferentes do DT_Arts": []})
    art_rows = []
    with tempfile.TemporaryDirectory(prefix="legaia_arts_") as workdir:
        for character in ("Vahn", "Noa", "Gala"):
            art_rows += art_hits(decode_record0(lzs_tool, args.extracted, character, workdir), character, arts_list, report)
    art_columns = ["Character", "ActionConstant", "HitBeats", "Rate", "RootSpeed", "EndStep"]

    print("[hero-battle] DT_ArtHits: %d Arts" % len(art_rows))
    for r in art_rows:
        print("            %-28s 0x%02X quadros %-14s rate %d" % (r["---"], r["ActionConstant"], r["HitBeats"], r["Rate"]))

    print("[hero-battle] DT_HeroReactions: %d heróis" % len(reaction_rows))
    for r in reaction_rows:
        print("            %-8s %s" % (r["---"], "  ".join("%s %d" % (c[:-6], r[c]) for c in reaction_columns[1:])))
    print("[hero-battle] DT_HeroSwings: %d linhas (%d padrão)" % (len(swing_rows), sum(1 for r in swing_rows if r["_default"])))
    for r in swing_rows:
        print("            %-34s poder %-9s quadros %-9s rate %d custo %d" % (
            r["---"], r["PowerBytes"], r["HitBeats"], r["Rate"], r["Cost"]))
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
                                 (COSTS_TABLE, ["Character", "Item", "ArmsCost"], cost_rows),
                                 (SWINGS_TABLE, ["Character", "Item", "Button", "PowerBytes", "HitBeats", "Rate", "Cost", "RootSpeed", "EndStep"],
                                  swing_rows),
                                 (REACTIONS_TABLE, reaction_columns, reaction_rows),
                                 (ART_HITS_TABLE, art_columns, art_rows)):
        if same_as_csv(table, columns, rows):
            print("[hero-battle] %s: sem mudança, não reescrita." % table)
            continue
        bak = lt.backup(table)
        if bak:
            print("[hero-battle] backup: " + bak)
        lt.write_rows(table, columns, rows)
        print("[hero-battle] OK: " + lt.csv_path(table))


if __name__ == "__main__":
    main()
