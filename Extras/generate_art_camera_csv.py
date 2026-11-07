#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_art_camera_csv.py — gera a tabela da câmera própria das Arts, lida do disco do usuário:

  Docs/Data/DT_ArtCamera.csv  (Row Struct: FJRPGArtCameraTrackData)  as 20 linhas da tabela de offsets que a câmera de cada
                                                                     Art soma na pose (pitch, giro, distância), duas colunas

    python Extras/generate_art_camera_csv.py --extracted <LegaiaRE/extracted> [--dry-run]

A tabela está no overlay de ação da luta (PROT 0898), no endereço 0x801F4E10 = arquivo +0x265F8: 20 linhas de dois
halfwords com sinal (little-endian), valor(linha, coluna) = base[linha * 4 + coluna * 2] (docs/formats/
battle-attack-camera-table.md do legend-of-legaia-re). A coluna é um cara-ou-coroa a cada ação (a mesma Art filmada de dois
jeitos). Quais linhas cada Art lê, e em que parte da pose, é código (AJRPGBattleCamera::BuildArtPose), não dado.

O par logo depois da tabela (0x801F4E60) é (0, 0): o script confere isso para não ler um disco / overlay diferente.
Só números saem daqui (regra 48: nada do disco além de números entra no git). O CSV anterior vai para Docs/Data/_backup/.
"""

import argparse
import glob
import os
import struct
import sys

import legaia_toml as lt

TABLE = "DT_ArtCamera"
COLUMNS = ["Value0", "Value1"]
FILE_OFFSET = 0x265F8
ROWS = 20


def read_tracks(extracted):
    matches = glob.glob(os.path.join(extracted, "PROT", "0898_*.BIN"))
    if not matches:
        sys.exit("Não achei PROT/0898_*.BIN em %s" % extracted)
    with open(matches[0], "rb") as f:
        data = f.read()
    end = FILE_OFFSET + (ROWS + 1) * 4
    if len(data) < end:
        sys.exit("PROT 0898 curto demais (%d bytes): não é o overlay de ação da luta" % len(data))
    rows = [struct.unpack_from("<hh", data, FILE_OFFSET + 4 * i) for i in range(ROWS)]
    if struct.unpack_from("<hh", data, FILE_OFFSET + 4 * ROWS) != (0, 0):
        sys.exit("O par depois da tabela não é (0, 0): o PROT 0898 não bate com o do RE (outra versão do disco?)")
    return rows


def main():
    ap = argparse.ArgumentParser(prog="generate_art_camera_csv")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract (PROT/)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    tracks = read_tracks(args.extracted)
    rows = [{"---": "Row%02d" % i, "Value0": str(a), "Value1": str(b)} for i, (a, b) in enumerate(tracks)]
    for r in rows:
        print("[art-camera] %s %6s %6s" % (r["---"], r["Value0"], r["Value1"]))
    if args.dry_run:
        print("[art-camera] dry-run: nada escrito")
        return
    bak = lt.backup(TABLE)
    if bak:
        print("[art-camera] backup: " + bak)
    lt.write_rows(TABLE, COLUMNS, rows)
    print("[art-camera] OK: " + lt.csv_path(TABLE))


if __name__ == "__main__":
    main()
