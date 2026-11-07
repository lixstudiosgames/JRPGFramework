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

bScriptedFight (a flag de luta roteirizada: o perfil de stats e a fuga, BATTLE_ROADMAP.md › 5.0) é
verdadeira em todos os chefes menos a Lapis; ver NOT_SCRIPTED.

Regenerar NÃO apaga trabalho manual: Enemies e bScriptedFight postos à mão são mantidos, e linhas criadas no
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
COLUMNS = ["Key", "DisplayName", "Location", "FightCode", "Enemies", "bScriptedFight", "HP", "MP",
           "SpecialAttacks", "SpecialAttackMP", "EXP", "Gold", "RewardItem", "RecommendedLevel",
           "Tournament", "Notes", "Music", "bKeepCurrentMusic"]
# Corrigidos à mão: mantidos numa regeneração
MANUAL_COLUMNS = ["Enemies", "bScriptedFight", "Music"]

# A flag de luta roteirizada do original (ctx+0x287) é da FORMAÇÃO, não do chefe: o byte de cabeçalho da
# linha do encontro. Todo chefe da história a liga (perfil de stats B, sem fuga, sem vantagem de
# formação), menos as lutas cuja linha tem o byte 0 — a Lapis (a linha do disco forçada, 0xA7) e o treino
# do Tetsu (town01, linha 4; RE docs/subsystems/battle.md, "Scripted-battle entry"), que usam o perfil do
# encontro aleatório. O treino NÃO está no bosses.toml (a Lapis sim): a luta dele é montada com
# FJRPGBattleSetup::bScriptedFight = false à mão. As lutas da Muscle Dome seguem os chefes que reusam.
NOT_SCRIPTED = {"lapis"}

# A música de cada luta (RowName no DT_BattleMusic), do levantamento do disco (2026-11-06): todo `3E FF <linha>` dos scripts das
# cenas (legaia-engine man-scripts --disasm-record), o op 0x35 sub-op 7 (o "sound set" do banco de luta) que o mesmo registro roda
# antes, e a linha da formação (os ids do monster archive -> DT_Enemies). Sound set 0..8 = Battle1 Battle2 Boss1 Songi Boss2 Cort1
# Koru2 VanSaryu Cort2. Sem sub-op 7 no registro, vale o da cena (0 = Battle1). O -1 (mantém a música) depois do evento pôr o 2028
# vira Boss1 aqui (o jogo não tem o evento tocando o tema antes). Fora do levantamento: Van Saryu (a luta não sai por 3E FF; a faixa
# 7 do banco é a "Vahn-Saryu" e nenhuma outra luta a usa) e as da Muscle Dome (ficam com a escolha comum).
MUSIC = {
    "caruban": "Boss1",          # rikuroa linha 17, set 2
    "vigurox2": "Battle1",       # vozz linha 10: o evento põe 2033, sem sub-op 7 -> o da cena (0)
    "songi_1st": "Songi",        # garmel linha 8, set 3
    "zeto": "Boss2",             # garmel linha 9, set 4
    "berserker": "Boss1",        # teien linha 0, set 2
    "xain": "Boss1",             # tunnelc linha 10, set 2
    "songi_2nd": "Songi",        # retona linha 11, set 3
    "van_saryu": "VanSaryu",     # inferido (ver acima)
    "dohati": "Boss2",           # dohaty linha 10, set 4
    "gaza": "Boss1",             # kor5 linha 14: 2028 e -1
    "gaza_sim_seru": "Boss1",    # korb3 linha 15: 2028 e -1
    "che_delilas": "Boss1",      # nilboa linha 29: 2028 e -1
    "gi_delilas": "Boss2",       # nilboa linha 31, set 4
    "koru": "Koru2",             # nilboa linha 32, set 6
    "zora": "Boss2",             # taiku linha 9, set 4
    "rogue": "Boss2",            # rugi linha 6, set 4
    "jette": "Boss2",            # chitei2 linha 12, set 4
    "cort": "Cort1",             # chitei2 linha 13, set 5
    "tetsu_rematch": "Battle1",  # town0d linha 4, sem sub-op 7
    "songi_3rd": "Songi",        # noaru linha 4, set 3
    "cort_juggernaut": "Cort2",  # jouine linha 21, set 8
    "lapis": "Boss1",            # deene linha 11, set 2
}
# O -1 sem tema de evento antes: a luta segue na música que já toca (a do duelo anterior)
KEEP_MUSIC = {"lu_delilas"}       # nilboa linha 30


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
            "bScriptedFight": lt.ue_bool(key not in NOT_SCRIPTED),
            "HP": b.get("hp_max") or 0, "MP": b.get("mp") or 0,
            "SpecialAttacks": lt.ue_array([a["name"] for a in attacks], quote=True),
            "SpecialAttackMP": lt.ue_array([a.get("mp") or 0 for a in attacks]),
            "EXP": b.get("exp_reward") or 0, "Gold": b.get("gold_reward") or 0, "RewardItem": item,
            "RecommendedLevel": b.get("recommended_level") or "", "Tournament": tournament or "",
            "Notes": b.get("notes", ""),
            "Music": MUSIC.get(key, ""), "bKeepCurrentMusic": lt.ue_bool(key in KEEP_MUSIC),
        })

    # Trabalho manual sobrevive à regeneração
    generated = {r["---"] for r in rows}
    for r in rows:
        old = old_rows.get(r["---"])
        for c in MANUAL_COLUMNS:
            if old and old.get(c, "") not in ("", "()") and old[c] != r[c]:
                r[c] = old[c]
                report["%s mantido do CSV (posto à mão)" % c].append(r["---"])
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
