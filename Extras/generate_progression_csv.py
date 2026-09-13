#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_progression_csv.py — gera DT_LevelCurve.csv e DT_Difficulty.csv.

    python Extras/generate_progression_csv.py

DT_LevelCurve
    A curva de XP do jogo original (US), levels 2..99. Em RUNTIME quem manda é
    a formula em UCoreSubsystem::GetXPForLevel — esta tabela existe para
    consulta e tuning no LegaiaStudio. Se voce ajustar valores la, peca para
    levar a mudanca para o C++.

DT_Difficulty
    Multiplicadores por dificuldade. Os mesmos valores estao embutidos como
    fallback no CoreSubsystem, entao o jogo funciona sem a DataTable — ela e o
    caminho para tunar sem recompilar.

IMPORTADOR DE UMA VEZ SO: rodar de novo SOBRESCREVE os CSVs inteiros.
"""

import csv
import os
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PLUGIN_DIR, "Docs", "Data")

MAX_LEVEL = 99


def xp_for_level(level):
    """
    XP acumulado para ALCANCAR `level`, contando do level 1.

    Formula de FUN_801E9504 (jogo original US):
        delta(n) = n*n/4 + 1            -> tabela DAT_80076AF4
        soma(L)  = delta(1) + ... + delta(L)
        limiar   = soma * 9999999 / 0x140FE   se L <  17
        limiar   = soma * 121                 se L >= 17
    onde L e o level de ORIGEM (level - 1).

    Validada contra o jogo: L2 = 121, L38 = 535546, L99 = 9646483.
    """
    if level <= 1:
        return 0
    level = min(level, MAX_LEVEL)
    frm = level - 1
    total = sum(n * n // 4 + 1 for n in range(1, frm + 1))
    return (total * 9999999) // 0x140FE if frm < 0x11 else total * 0x79


def write_csv(name, columns, rows):
    path = os.path.join(DATA_DIR, name + ".csv")
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["---"] + columns)
        w.writerows(rows)
    print("[progression] OK: %s (%d linhas)" % (path, len(rows)))
    return path


def build_level_curve():
    columns = ["Level", "XPTotal", "XPFromPrevious"]
    rows = []
    previous = 0
    for level in range(2, MAX_LEVEL + 1):
        total = xp_for_level(level)
        rows.append([str(level), str(level), str(total), str(total - previous)])
        previous = total
    return columns, rows


def build_difficulty():
    """
    Multiplicadores por dificuldade.

    OS TRES EIXOS NAO ANDAM JUNTOS, e isso e proposital. A formula de dano do
    Legaia e SUBTRATIVA (Dano = Ofensa - Defesa), entao cada um se comporta de
    um jeito completamente diferente:

      EnemyHP   linear e previsivel — e o knob honesto de dificuldade, pode
                subir bastante sem quebrar nada.

      EnemyDEF  SATURA. Quando a sua ofensa nao vence a defesa, o jogo reescreve
                a ofensa por cima da defesa e ela CANCELA na subtracao: o dano
                vira ~29% da sua propria ofensa, independente do tamanho da
                defesa. Acima de ~1.5x o multiplicador nao faz mais efeito
                nenhum — so empurra o jogador para esse regime, onde o retorno
                marginal do ATK cai de 1:1 para ~0.29:1 e a progressao de arma
                rende menos. Por isso fica BAIXO.

      EnemyATK  NAO tem piso do lado do jogador: bate na UDF/LDF pela mesma
                subtracao, sem a rede do underdog. E o mais perigoso dos tres —
                por isso e o MENOR multiplicador.

    Com os tres iguais a 2.5 (como estava antes), o Juggernaut ficava
    matematicamente perdido: as lutas duravam 4.8x mais ENQUANTO o jogador
    tomava 5.7x por golpe, e o inimigo matava primeiro. Ver a analise em
    Docs/Referencia/progressao.md.

    Row name = nome do valor de EJRPGDifficulty. Normal = jogo original.
    """
    columns = ["DisplayName", "Description", "EnemyHP", "EnemyATK", "EnemyDEF",
               "XPReward", "GoldReward"]
    rows = [
        ["Normal", "Normal", "O balanceamento do jogo original.",
         "1.0", "1.0", "1.0", "1.0", "1.0"],
        ["Hard", "Hard", "Inimigos aguentam bem mais e batem mais forte.",
         "1.8", "1.2", "1.25", "1.25", "1.25"],
        ["Juggernaut", "Juggernaut", "Cada encontro e uma ameaca real.",
         "3.0", "1.45", "1.4", "1.5", "1.5"],
    ]
    return columns, rows


def main():
    if not os.path.isdir(DATA_DIR):
        print("ERRO: nao encontrei %s" % DATA_DIR)
        return 1

    cols, rows = build_level_curve()
    write_csv("DT_LevelCurve", cols, rows)
    print("[progression]   L2=%d  L38=%d  L99=%d"
          % (xp_for_level(2), xp_for_level(38), xp_for_level(MAX_LEVEL)))

    cols, rows = build_difficulty()
    write_csv("DT_Difficulty", cols, rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
