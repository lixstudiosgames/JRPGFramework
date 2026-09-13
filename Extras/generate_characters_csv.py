#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_characters_csv.py — gera Docs/Data/DT_Characters.csv a partir de
Docs/Data/gamedata/characters.toml.

IMPORTADOR DE UMA VEZ SÓ. Depois que a DataTable estiver importada na Unreal,
edite pelo LegaiaStudio: rodar este script de novo SOBRESCREVE o CSV inteiro e
apaga qualquer ajuste manual.

    python Extras/generate_characters_csv.py

O header do CSV são as UPROPERTY de FCharacterData (Party/CharacterData.h), na
ordem de declaração — é o que a Unreal espera na importação.
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
TOML_PATH = os.path.join(PLUGIN_DIR, "Docs", "Data", "gamedata", "characters.toml")
CSV_PATH = os.path.join(PLUGIN_DIR, "Docs", "Data", "DT_Characters.csv")

# Colunas = UPROPERTY de FCharacterData, na ordem do header C++
COLUMNS = ["Key", "XPCurveSlot", "DisplayName", "PortraitId", "RaSeru", "RaSeruElement",
           "AffinityStrong", "AffinityWeak", "WeaponClasses", "bLeftHanded",
           "BaseHP", "BaseMP", "BaseAGL", "BaseATK", "BaseUDF", "BaseLDF",
           "BaseSPD", "BaseINT", "Notes"]

# Atributos iniciais (level 1) do jogo original US.
#
# Fonte: tabela de new game em SCUS_942.54, base 0x80078C4C, stride 26 bytes
# (8 x u16 + nome de 10 bytes), decodificada em
# LegaiaRE/docs/formats/new-game-table.md. HP e MP sao tambem o maximo inicial.
#
# Terra esta na tabela do disco (4o slot) mas nao no characters.toml — se ela
# virar personagem jogavel, e so acrescentar a entrada la e a linha aqui:
#   Terra: 400 / 200 / 200 / 45 / 20 / 17 / 45 / 25
BASE_STATS = {
    #        HP    MP  AGL  ATK  UDF  LDF  SPD  INT
    "Vahn":  (180,  20, 100,  24,  16,  12,  19,   9),
    "Noa":   (150,  10, 120,  21,  13,  11,  30,   3),
    "Gala":  (210,  40,  80,  30,  43,  30,  15,  20),
    "Terra": (400, 200, 200,  45,  20,  17,  45,  25),
}

# elemento do TOML (minúsculo) -> valor de EJRPGElement
# Slot da curva de XP do original: so os slots 1 e 2 levam correcao de limiar
# (Noa sobe mais cedo, Gala mais tarde). Ver Docs/Referencia/progressao.md.
SLOT_DA_CURVA = {"Vahn": 0, "Noa": 1, "Gala": 2, "Terra": 3}

ELEMENTS = {
    "fire": "Fire", "wind": "Wind", "thunder": "Thunder", "water": "Water",
    "earth": "Earth", "light": "Light", "dark": "Dark", "evil": "Evil",
}


def parse_toml(path):
    """
    Parser mínimo de [[character]] — mesmo estilo do generate_csv.py, para o
    script não depender de tomllib nem de pip. Cobre string, bool e array de
    strings, que é tudo o que o characters.toml usa.
    """
    entries = []
    current = None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[[") and line.endswith("]]"):
                if current is not None:
                    entries.append(current)
                current = {}
                continue
            if current is None or "=" not in line:
                continue

            key, value = (p.strip() for p in line.split("=", 1))
            if value.startswith('"') and value.endswith('"'):
                current[key] = value[1:-1]
            elif value.startswith("[") and value.endswith("]"):
                inner = value[1:-1].strip()
                current[key] = [v.strip().strip('"') for v in inner.split(",") if v.strip()] if inner else []
            elif value in ("true", "false"):
                current[key] = (value == "true")
            else:
                current[key] = value

    if current is not None:
        entries.append(current)
    return entries


def element(name):
    slug = str(name or "").strip().lower()
    if not slug:
        return "None"
    mapped = ELEMENTS.get(slug)
    if not mapped:
        print("  AVISO: elemento desconhecido '%s' — virou None." % name)
        return "None"
    return mapped


def base_stats(key):
    """Atributos iniciais do personagem. Sem entrada conhecida, devolve zeros."""
    stats = BASE_STATS.get(key)
    if stats is None:
        print("  AVISO: sem atributos iniciais para '%s' — zerados." % key)
        return (0,) * 8
    return stats


def unreal_array(values, quote=False):
    """
    ['Fire','Water'] -> (Fire,Water). Vazio -> celula vazia.

    ARRAY DE FString PRECISA DE ASPAS EM CADA ITEM: o importador de CSV da
    Unreal recusa ("Missing opening '\"' in string property value") quando os
    elementos vem crus. Array de ENUM e o contrario — nao leva aspas.
    Compare com DT_Items, que ja grava ("Vahn","Noa") em EquipOthers.
    """
    clean = [str(v).strip() for v in (values or []) if str(v).strip()]
    if not clean:
        return ""
    if quote:
        clean = ['"%s"' % v.replace('"', '""') for v in clean]
    return "(%s)" % ",".join(clean)


def main():
    if not os.path.isfile(TOML_PATH):
        print("ERRO: não encontrei %s" % TOML_PATH)
        return 1

    chars = parse_toml(TOML_PATH)
    if not chars:
        print("ERRO: nenhum [[character]] em %s" % TOML_PATH)
        return 1

    rows = []
    for c in chars:
        key = c.get("key", "")
        if not key:
            print("  AVISO: personagem sem 'key' — ignorado.")
            continue

        rows.append([
            key,                                    # RowName
            key,                                    # Key
            str(SLOT_DA_CURVA.get(key, 0)),         # XPCurveSlot
            c.get("name", key),                     # DisplayName
            key.lower(),                            # PortraitId -> images/vahn.png
            c.get("ra_seru", ""),
            element(c.get("ra_seru_element")),
            unreal_array([element(e) for e in c.get("affinity_strong", [])]),
            unreal_array([element(e) for e in c.get("affinity_weak", [])]),
            unreal_array(c.get("weapon_classes", []), quote=True),  # TArray<FString>
            "True" if c.get("left_handed") else "False",
        ] + [str(v) for v in base_stats(key)] + [
            c.get("notes", ""),
        ])

    # CRLF como os demais CSVs de DataTable do projeto
    with open(CSV_PATH, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["---"] + COLUMNS)
        w.writerows(rows)

    print("[characters] OK: %s" % CSV_PATH)
    print("[characters]   %d personagens: %s" % (len(rows), ", ".join(r[0] for r in rows)))
    print("[characters]   colunas: %s" % ", ".join(COLUMNS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
