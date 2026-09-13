#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_shops_csv.py — Converte Docs/Data/ITENS DATA/shops.toml em DT_Shops.csv
para importar na Unreal como DataTable (Row Struct: FShopData).

Uso:
    python Extras/generate_shops_csv.py

Regras de conversão:
- Row name (primeira coluna) = slug único: town + (name|merchant) [+ _before|_after].
- `phase` vira a condição default (o usuário troca a flag na DataTable como quiser):
    "before mist" -> RequiredFlagID=mist_cleared, bRequiredFlagValue=False
    "after mist"  -> RequiredFlagID=mist_cleared, bRequiredFlagValue=True
    sem phase     -> RequiredFlagID vazio (loja sempre disponível)
- `featured` são os itens gated pelo platinum_card (invisíveis sem o card).
- `notes` é ignorado (o gate do Karisto Camper já está coberto pelos featured).
- Cada key do inventory é validada contra os TOMLs de itens (warning se não existir).
- Arrays no CSV usam o formato da UE: "(a,b,c)".

Sem dependências externas (stdlib only).
"""

import csv
import os
import re
import sys

DOCS_DATA_DIR = r"d:\JOGOS\PROJETO LEGAIA\CODE\JRPGFramework\Docs\Data"
ITENS_DATA_DIR = os.path.join(DOCS_DATA_DIR, "ITENS DATA")
SHOPS_TOML = os.path.join(ITENS_DATA_DIR, "shops.toml")
OUT_CSV = os.path.join(DOCS_DATA_DIR, "DT_Shops.csv")

ITEM_TOML_FILES = ["items.toml", "weapons.toml", "armor.toml", "accessories.toml"]

# phase -> (RequiredFlagID, bRequiredFlagValue)
PHASE_TO_FLAG = {
    "before mist": ("mist_cleared", "False"),
    "after mist": ("mist_cleared", "True"),
}


def strip_comment(line):
    """Remove comentário (#) fora de aspas."""
    in_string = False
    for i, ch in enumerate(line):
        if ch == '"':
            in_string = not in_string
        elif ch == '#' and not in_string:
            return line[:i]
    return line


def parse_shops_toml(filepath):
    """
    Parser mínimo para o shops.toml: blocos [[shop]] com chaves string e
    arrays de strings que podem abranger VÁRIAS linhas.
    """
    shops = []
    current = None

    with open(filepath, "r", encoding="utf-8") as f:
        raw_lines = f.readlines()

    # Junta continuações de arrays multi-linha em declarações lógicas únicas
    logical_lines = []
    buffer = ""
    for raw in raw_lines:
        line = strip_comment(raw).strip()
        if not line:
            continue
        if buffer:
            buffer += " " + line
        else:
            buffer = line
        # Declaração completa quando os colchetes se equilibram
        if buffer.count("[") == buffer.count("]") or (
            "=" not in buffer and buffer.startswith("[[")
        ):
            logical_lines.append(buffer)
            buffer = ""
    if buffer:
        logical_lines.append(buffer)

    for line in logical_lines:
        if line == "[[shop]]":
            current = {}
            shops.append(current)
            continue
        if current is None or "=" not in line:
            continue

        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip()

        if value.startswith("["):
            inner = value.strip()[1:-1]  # remove [ ]
            items = [v.strip().strip('"') for v in inner.split(",")]
            current[key] = [v for v in items if v]
        else:
            current[key] = value.strip('"')

    return shops


def collect_valid_item_keys():
    """Extrai todas as `key = "..."` dos TOMLs de itens para validação."""
    keys = set()
    key_re = re.compile(r'^\s*key\s*=\s*"([^"]+)"')
    for fname in ITEM_TOML_FILES:
        path = os.path.join(ITENS_DATA_DIR, fname)
        if not os.path.isfile(path):
            print(f"[shops_csv] AVISO: {fname} não encontrado — validação parcial.")
            continue
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                m = key_re.match(line)
                if m:
                    keys.add(m.group(1))
    return keys


def slugify(text):
    text = text.lower()
    text = text.replace("'", "")
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_")


def make_row_name(shop):
    town = slugify(shop.get("town", "unknown"))
    label = shop.get("name") or shop.get("merchant") or "shop"
    slug = f"{town}_{slugify(label)}"

    phase = shop.get("phase", "")
    if phase.startswith("before"):
        slug += "_before"
    elif phase.startswith("after"):
        slug += "_after"
    return slug


def to_ue_array(items):
    """Formato de TArray<FName> no CSV da UE: (a,b,c). Vazio = string vazia."""
    if not items:
        return ""
    return "(" + ",".join(items) + ")"


def main():
    if not os.path.isfile(SHOPS_TOML):
        print(f"[shops_csv] ERRO: {SHOPS_TOML} não encontrado.")
        sys.exit(1)

    shops = parse_shops_toml(SHOPS_TOML)
    valid_keys = collect_valid_item_keys()

    rows = []
    used_names = {}
    warnings = 0

    for shop in shops:
        town = shop.get("town", "")
        name = shop.get("name", "")
        merchant = shop.get("merchant", "")
        inventory = shop.get("inventory", [])
        featured = shop.get("featured", [])
        phase = shop.get("phase", "")

        if not inventory:
            print(f"[shops_csv] AVISO: loja sem inventory em {town} ({name or merchant}).")
            warnings += 1

        # Valida keys contra os TOMLs de itens
        for key in inventory + featured:
            if valid_keys and key not in valid_keys:
                print(f"[shops_csv] AVISO: item '{key}' ({town}) não existe nos TOMLs de itens.")
                warnings += 1

        # Featured deve ser subconjunto do inventory
        for key in featured:
            if key not in inventory:
                print(f"[shops_csv] AVISO: featured '{key}' ({town}) não está no inventory da loja.")
                warnings += 1

        # Slug único
        row_name = make_row_name(shop)
        if row_name in used_names:
            used_names[row_name] += 1
            row_name = f"{row_name}_{used_names[row_name]}"
            print(f"[shops_csv] AVISO: slug duplicado — renomeado para '{row_name}'.")
            warnings += 1
        else:
            used_names[row_name] = 1

        flag_id, flag_value = PHASE_TO_FLAG.get(phase, ("", "True"))
        if phase and phase not in PHASE_TO_FLAG:
            print(f"[shops_csv] AVISO: phase desconhecida '{phase}' ({town}) — sem condição.")
            warnings += 1

        display_name = name or merchant or "Shop"

        rows.append({
            "---": row_name,
            "ShopName": display_name,
            "Town": town,
            "Merchant": merchant,
            "Inventory": to_ue_array(inventory),
            "FeaturedItems": to_ue_array(featured),
            "RequiredFlagID": flag_id,
            "bRequiredFlagValue": flag_value,
        })

    fieldnames = ["---", "ShopName", "Town", "Merchant", "Inventory",
                  "FeaturedItems", "RequiredFlagID", "bRequiredFlagValue"]

    with open(OUT_CSV, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    gated = sum(1 for r in rows if r["RequiredFlagID"])
    with_featured = sum(1 for r in rows if r["FeaturedItems"])
    print(f"[shops_csv] OK: {len(rows)} lojas geradas em {OUT_CSV}")
    print(f"[shops_csv]   com condição (phase): {gated} | com featured: {with_featured} | avisos: {warnings}")


if __name__ == "__main__":
    main()
