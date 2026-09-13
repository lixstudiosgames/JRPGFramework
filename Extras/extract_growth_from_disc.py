#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
extract_growth_from_disc.py — le as tabelas de crescimento direto do disco do
jogo e gera DT_StatGrowth.csv e DT_GrowthCurve.csv.

    python Extras/extract_growth_from_disc.py "G:\\JOGOS\\EMU\\PS1\\Legend of Legaia (USA).bin"
    python Extras/extract_growth_from_disc.py caminho/para/SCUS_942.54

Aceita a imagem .bin (raw Mode 2 Form 1) ou o SCUS_942.54 ja extraido.

POR QUE ESTE SCRIPT EXISTE: os parametros de crescimento {start, max, jitter,
row} moram no executavel do jogo. O LegaiaRE documenta a estrutura mas nao
versiona os bytes (copyright) — quem tem o disco extrai. Rodar isto uma vez
preenche as tabelas; depois e so editar no LegaiaStudio.

Mapeamento VA -> offset: o PS-EXE carrega em 0x80010000 e o corpo comeca em
0x800 do arquivo, entao offset = VA - 0x8000F800. Validado contra o valor
publicado no doc do LegaiaRE (VA 0x80070A3C == offset 0x6123C).
"""

import csv
import os
import struct
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PLUGIN_DIR, "Docs", "Data")

# --- ISO9660 / CD raw ---
RAW_SECTOR = 2352
DATA_OFFSET = 24          # sync(12) + header(4) + subheader(8)
DATA_SIZE = 2048

# --- Enderecos das tabelas (VA do SCUS) ---
VA_XP_DELTAS = 0x80076AF4     # 98 x u16 — deltas da curva de XP
VA_CURVES = 0x800769CC        # 3 rows x 98 bytes — curvas de crescimento
VA_CHAR_PARAMS = 0x80076918   # stride 0x3C — {u16 start, u16 max, u8 jitter, u8 row} x8

CURVE_ROWS = 3
CURVE_STRIDE = 0x62           # 98 = MAX_LEVEL - 1
CURVE_SUM = 0x24C0            # 9408 — normalizador; cada curva soma exatamente isto

STATS = ["HP", "MP", "AGL", "ATK", "UDF", "LDF", "SPD", "INT"]

# Personagens COM parametros no disco. O bloco DAT_80076918 tem exatamente 3
# registros: o que vem depois do terceiro ja e outra estrutura (valores subindo
# de 3084 em 3084, cara de tabela de offsets). A Terra, que e o 4o slot do
# roster, nao tem parametros de crescimento no jogo original.
CHARS = ["Vahn", "Noa", "Gala"]

# --------------------------------------------------------------------------
# NAO SAO DADOS DO DISCO — parametros DESENHADOS por nos.
#
# A Terra (a loba que criou a Noa, indice 3 do roster) tem atributos iniciais
# reais na tabela de new game (400/200/200/45/20/17/45/25), mas NAO tem
# parametros de crescimento: o bloco do disco acaba no terceiro personagem.
# Para ela ser jogavel de verdade precisamos inventar os tetos.
#
# Criterio: ela entra forte e estaciona. Comeca na frente em 6 dos 8 atributos
# e no L99 sobra so AGL e SPD (loba: rapida ate o fim), com o MENOR teto de HP
# do grupo — 4135 contra 4445 da Noa e 5245 do Gala. Jitter e CurveRow seguem o
# padrao dos tres do disco: jitter 4 no HP e 1 no resto, curva 1 no AGL.
#
# MinDifficulty = "Hard": no NORMAL ela NAO sobe de atributo, reproduzindo o
# original (nao subir de level e o motivo de nao existirem parametros dela no
# disco). No Hard e no Juggernaut ela cresce, para ter como ajudar de verdade.
#
# Mexer aqui muda o balanceamento. Depois de mexer, rode o script de novo.
DESIGNED = {
    "Terra": {
        #        start  max  jitter  curva  dificuldade minima
        "HP":   (400, 4200, 4, 0, "Hard"),
        "MP":   (200,  700, 1, 0, "Hard"),
        "AGL":  (200,  290, 1, 1, "Hard"),
        "ATK":  ( 45,  430, 1, 0, "Hard"),
        "UDF":  ( 20,  380, 1, 0, "Hard"),
        "LDF":  ( 17,  360, 1, 0, "Hard"),
        "SPD":  ( 45,  520, 1, 1, "Hard"),
        "INT":  ( 25,  360, 1, 0, "Hard"),
    },
}


def read_sector(f, lba):
    f.seek(lba * RAW_SECTOR + DATA_OFFSET)
    return f.read(DATA_SIZE)


def read_range(f, lba, size):
    out = bytearray()
    while len(out) < size:
        out += read_sector(f, lba)
        lba += 1
    return bytes(out[:size])


def scus_from_bin(path):
    """Acha e extrai o SCUS_942.54 de uma imagem .bin raw."""
    with open(path, "rb") as f:
        pvd = read_sector(f, 16)
        if pvd[1:6] != b"CD001":
            raise ValueError("setor 16 sem CD001 — a imagem nao parece raw Mode 2")

        root = pvd[156:156 + 34]
        root_lba = struct.unpack_from("<I", root, 2)[0]
        root_size = struct.unpack_from("<I", root, 10)[0]

        data = read_range(f, root_lba, root_size)
        pos = 0
        while pos < len(data):
            length = data[pos]
            if length == 0:
                pos = (pos // DATA_SIZE + 1) * DATA_SIZE
                if pos >= len(data):
                    break
                continue
            rec = data[pos:pos + length]
            name_len = rec[32]
            name = rec[33:33 + name_len].decode("ascii", "replace").split(";")[0]
            if name.upper().startswith("SCUS"):
                lba = struct.unpack_from("<I", rec, 2)[0]
                size = struct.unpack_from("<I", rec, 10)[0]
                print("[growth] SCUS encontrado na imagem: %s (%d bytes)" % (name, size))
                return read_range(f, lba, size)
            pos += length
    raise ValueError("SCUS nao encontrado na raiz da imagem")


def load_scus(path):
    with open(path, "rb") as f:
        head = f.read(8)
    if head == b"PS-X EXE":
        print("[growth] usando SCUS ja extraido: %s" % path)
        return open(path, "rb").read()
    return scus_from_bin(path)


def va_to_offset(blob, va):
    """offset = VA - (endereco de carga - 0x800), lido do cabecalho PS-EXE."""
    load = struct.unpack_from("<I", blob, 0x18)[0]
    return va - (load - 0x800)


def read_existing_rows(name):
    """Linhas ja no CSV, por RowName — para nao perder o que nao vem do disco."""
    path = os.path.join(DATA_DIR, name + ".csv")
    if not os.path.isfile(path):
        return []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    return rows[1:] if rows else []


def write_csv(name, columns, rows):
    path = os.path.join(DATA_DIR, name + ".csv")
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["---"] + columns)
        w.writerows(rows)
    print("[growth] OK: %s (%d linhas)" % (path, len(rows)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    src = sys.argv[1]
    if not os.path.isfile(src):
        print("ERRO: nao achei %s" % src)
        return 1

    blob = load_scus(src)
    off = lambda va: va_to_offset(blob, va)

    # --- 1. Confere a curva de XP contra a forma fechada -----------------
    deltas = list(struct.unpack_from("<98H", blob, off(VA_XP_DELTAS)))
    closed = [n * n // 4 + 1 for n in range(1, 99)]
    if deltas == closed:
        print("[growth] curva de XP do disco confere com delta(n)=n^2/4+1.")
    else:
        print("[growth] AVISO: a curva de XP do disco NAO bate com a forma fechada!")
        print("         disco  : %s" % deltas[:8])
        print("         formula: %s" % closed[:8])

    # --- 2. Curvas de crescimento ---------------------------------------
    curves = []
    base = off(VA_CURVES)
    for row in range(CURVE_ROWS):
        curve = list(blob[base + row * CURVE_STRIDE: base + (row + 1) * CURVE_STRIDE])
        if sum(curve) != CURVE_SUM:
            print("[growth] AVISO: curva %d soma %d, esperado %d — endereco errado?"
                  % (row, sum(curve), CURVE_SUM))
        curves.append(curve)

    rows = []
    for i in range(CURVE_STRIDE):
        level = i + 1                     # curve[row][level-1]
        rows.append([str(level), str(level)] + [str(c[i]) for c in curves])
    write_csv("DT_GrowthCurve", ["Level", "Row0", "Row1", "Row2"], rows)

    # --- 3. Parametros por personagem ------------------------------------
    # Antes de reescrever, guarda o que ja estava la: personagem que nao vem do
    # disco nem esta em DESIGNED (um que voce tenha criado no LegaiaStudio, por
    # exemplo) sobrevive a uma nova rodada do extrator.
    conhecidos = set(CHARS) | set(DESIGNED)
    preservadas = [r for r in read_existing_rows("DT_StatGrowth")
                   if len(r) > 1 and r[1] not in conhecidos]

    rows = []
    base = off(VA_CHAR_PARAMS)
    for slot, name in enumerate(CHARS):
        rec = base + slot * 0x3C
        for i, stat in enumerate(STATS):
            start, mx, jitter, row = struct.unpack_from("<HHBB", blob, rec + i * 6)
            rows.append([
                "%s_%s" % (name, stat),   # RowName
                name, stat,
                str(start), str(mx), str(jitter), str(row),
                "Normal",                 # os do original crescem em toda dificuldade
            ])

    for name, stats in DESIGNED.items():
        for stat in STATS:
            start, mx, jitter, row, min_diff = stats[stat]
            rows.append([
                "%s_%s" % (name, stat),
                name, stat,
                str(start), str(mx), str(jitter), str(row),
                min_diff,
            ])
        print("[growth] %s: 8 atributos DESENHADOS (nao vem do disco)." % name)

    if preservadas:
        rows.extend(preservadas)
        print("[growth] %d linha(s) preservadas de personagens de fora do disco."
              % len(preservadas))

    write_csv("DT_StatGrowth",
              ["Character", "Stat", "GrowthStart", "MaxValue", "Jitter", "CurveRow",
               "MinDifficulty"],
              rows)

    print()
    print("[growth] Pronto. O `GrowthStart` e a ancora da formula de crescimento e")
    print("[growth] pode diferir do atributo inicial em DT_Characters: o jogo retoca")
    print("[growth] o template de entrada de Vahn e Noa. Gala bate nos 8.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
