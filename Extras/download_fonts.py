#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
download_fonts.py — Verifica (e baixa, se preciso) as fontes locais da WebUI.

Uso:
    python Extras/download_fonts.py            # verifica e baixa o que faltar
    python Extras/download_fonts.py --check    # só verifica, não baixa nada

Fontes requeridas pelo CSS do shell (Content/UI/WebUI/src/css/00_global.css):
    Cinzel (UI geral)        — pesos 400/500/600/700/800/900 (TTFs estáticos)
    MedievalSharp (diálogos) — peso 400

Verificação: presença + magic bytes de TTF/OTF + a família certa na
tabela 'name' do arquivo. Download: fontes estáticas de peso único vêm do
repositório oficial github.com/google/fonts (raw); os pesos estáticos do
Cinzel (família variável no repo) vêm do zip oficial do fonts.google.com.
Stdlib only.
"""

import io
import struct
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS_DIR = ROOT / "Content" / "UI" / "WebUI" / "fonts"

GITHUB_RAW = "https://raw.githubusercontent.com/google/fonts/main/{path}"
GFONTS_ZIP = "https://fonts.google.com/download?family={family}"

# arquivo esperado -> (família na tabela 'name', estratégia de download)
# estratégia: ("raw", caminho no repo google/fonts) ou ("zip", família, membro no zip)
REQUIRED = {
    "Cinzel-Regular.ttf":   ("Cinzel", ("zip", "Cinzel", "static/Cinzel-Regular.ttf")),
    "Cinzel-Medium.ttf":    ("Cinzel", ("zip", "Cinzel", "static/Cinzel-Medium.ttf")),
    "Cinzel-SemiBold.ttf":  ("Cinzel", ("zip", "Cinzel", "static/Cinzel-SemiBold.ttf")),
    "Cinzel-Bold.ttf":      ("Cinzel", ("zip", "Cinzel", "static/Cinzel-Bold.ttf")),
    "Cinzel-ExtraBold.ttf": ("Cinzel", ("zip", "Cinzel", "static/Cinzel-ExtraBold.ttf")),
    "Cinzel-Black.ttf":     ("Cinzel", ("zip", "Cinzel", "static/Cinzel-Black.ttf")),
    "MedievalSharp-Regular.ttf": ("MedievalSharp", ("raw", "ofl/medievalsharp/MedievalSharp-Regular.ttf")),
}

# Opcionais: não exigidas pelo CSS atual, mas disponíveis caso a direção de
# arte volte a usar Cinzel Decorative (era usada no nome do personagem).
OPTIONAL = {
    "CinzelDecorative-Regular.ttf": ("Cinzel Decorative", ("raw", "ofl/cinzeldecorative/CinzelDecorative-Regular.ttf")),
    "CinzelDecorative-Bold.ttf":    ("Cinzel Decorative", ("raw", "ofl/cinzeldecorative/CinzelDecorative-Bold.ttf")),
    "CinzelDecorative-Black.ttf":   ("Cinzel Decorative", ("raw", "ofl/cinzeldecorative/CinzelDecorative-Black.ttf")),
}

SFNT_MAGICS = (b"\x00\x01\x00\x00", b"OTTO", b"true")


def read_name_table_families(data):
    """Extrai os nomes de família (nameID 1 e 16) da tabela 'name' de um sfnt."""
    families = set()
    try:
        num_tables = struct.unpack(">H", data[4:6])[0]
        name_offset = None
        for i in range(num_tables):
            rec = data[12 + i * 16: 12 + (i + 1) * 16]
            tag, _checksum, offset, _length = struct.unpack(">4sIII", rec)
            if tag == b"name":
                name_offset = offset
                break
        if name_offset is None:
            return families

        count, string_offset = struct.unpack(">HH", data[name_offset + 2: name_offset + 6])
        storage = name_offset + string_offset
        for i in range(count):
            rec_off = name_offset + 6 + i * 12
            platform_id, _enc, _lang, name_id, length, offset = struct.unpack(
                ">HHHHHH", data[rec_off: rec_off + 12])
            if name_id not in (1, 16):
                continue
            raw = data[storage + offset: storage + offset + length]
            if platform_id in (0, 3):  # Unicode / Windows -> UTF-16BE
                families.add(raw.decode("utf-16-be", errors="ignore"))
            else:  # Macintosh -> latin-1
                families.add(raw.decode("latin-1", errors="ignore"))
    except (struct.error, IndexError):
        pass
    return families


def verify_font(path, expected_family):
    """Retorna (ok, motivo)."""
    if not path.is_file():
        return False, "arquivo ausente"
    data = path.read_bytes()
    if len(data) < 12 * 1024:
        return False, f"arquivo suspeito de corrompido ({len(data)} bytes)"
    if not data.startswith(SFNT_MAGICS):
        return False, "não é um TTF/OTF válido (magic bytes)"
    families = read_name_table_families(data)
    if families and not any(expected_family.lower() in f.lower() for f in families):
        return False, f"família errada: esperado '{expected_family}', encontrado {sorted(families)}"
    return True, "OK"


def http_get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (JRPGFramework font fetch)"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


_zip_cache = {}


def download_font(filename, strategy):
    kind = strategy[0]
    if kind == "raw":
        url = GITHUB_RAW.format(path=strategy[1])
        print(f"    baixando {url}")
        return http_get(url)

    # kind == "zip": zip oficial da família no fonts.google.com (contém static/)
    family, member = strategy[1], strategy[2]
    if family not in _zip_cache:
        url = GFONTS_ZIP.format(family=family.replace(" ", "%20"))
        print(f"    baixando zip da família {family}: {url}")
        _zip_cache[family] = zipfile.ZipFile(io.BytesIO(http_get(url)))
    zf = _zip_cache[family]
    # O layout do zip pode variar (com/sem pasta static/) — procura pelo basename
    basename = member.split("/")[-1]
    for name in zf.namelist():
        if name.endswith(basename):
            return zf.read(name)
    raise FileNotFoundError(f"'{basename}' não encontrado no zip da família {family}")


def process(table, label, check_only, required):
    problems = 0
    for filename, (family, strategy) in table.items():
        path = FONTS_DIR / filename
        ok, reason = verify_font(path, family)
        if ok:
            print(f"  [OK]    {filename}")
            continue

        if not required and not path.is_file():
            print(f"  [--]    {filename} (opcional, não instalado)")
            continue

        print(f"  [FALHA] {filename}: {reason}")
        if check_only:
            problems += 1
            continue

        try:
            data = download_font(filename, strategy)
            FONTS_DIR.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            ok, reason = verify_font(path, family)
            if ok:
                print(f"  [BAIXADO] {filename} ({len(data) / 1024:.0f} KB)")
            else:
                print(f"  [ERRO]  {filename} baixado mas inválido: {reason}")
                problems += 1
        except Exception as e:  # rede indisponível, URL mudou, etc.
            print(f"  [ERRO]  {filename}: download falhou: {e}")
            problems += 1
    return problems


def main():
    check_only = "--check" in sys.argv
    print(f"[download_fonts] pasta: {FONTS_DIR}")

    print("[download_fonts] Fontes requeridas pelo shell:")
    problems = process(REQUIRED, "requeridas", check_only, required=True)

    print("[download_fonts] Fontes opcionais (Cinzel Decorative):")
    process(OPTIONAL, "opcionais", check_only=True, required=False)

    if problems:
        print(f"[download_fonts] {problems} problema(s) — o shell pode cair no fallback serif.")
        sys.exit(1)
    print("[download_fonts] Todas as fontes requeridas estão OK.")


if __name__ == "__main__":
    main()
