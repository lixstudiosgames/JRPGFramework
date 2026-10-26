"""
Gera os ícones de editor (billboards) dos atores de batalha do Field.

    python Extras/generate_editor_icons.py

Saída em Extras/EditorIcons/, 128x128 RGBA:
  T_FieldEnemyIcon.png    — círculo vermelho com dois olhos bravos (AJRPGFieldEnemy)
  T_EncounterZoneIcon.png — anel vermelho com três pontos (AJRPGEncounterZone)

Os ícones da engine no UE5 são cinza, e o billboard não tinge: o vermelho de "inimigo" precisa
estar na textura. Importar no Unreal em /JRPGFramework/Editor/ com Compression = EditorIcon e
Mip Gen = NoMipmaps (o C++ carrega pelos nomes acima).

Só biblioteca padrão. Mesmo código = mesmo arquivo, byte a byte.
"""
import math
import struct
import sys
import zlib
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

SIZE = 128
SUPERSAMPLE = 4   # amostras por eixo, para as bordas não serrilharem
OUT_DIR = Path(__file__).resolve().parent / "EditorIcons"

RED = (220, 40, 40)
DARK = (60, 10, 10)
WHITE = (255, 255, 255)


def inside_triangle(px, py, a, b, c):
    def side(p1, p2, x, y):
        return (x - p2[0]) * (p1[1] - p2[1]) - (p1[0] - p2[0]) * (y - p2[1])
    d1, d2, d3 = side(a, b, px, py), side(b, c, px, py), side(c, a, px, py)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def enemy(x, y):
    """Coordenadas -1..1, y para baixo. Devolve (r,g,b,a) ou None."""
    r = math.hypot(x, y)
    if r > 0.95:
        return None
    if r > 0.82:
        return DARK + (255,)
    # Olhos bravos: triângulos inclinados para o centro
    if inside_triangle(x, y, (-0.62, -0.30), (-0.12, -0.05), (-0.55, 0.12)):
        return WHITE + (255,)
    if inside_triangle(x, y, (0.62, -0.30), (0.12, -0.05), (0.55, 0.12)):
        return WHITE + (255,)
    # Boca: uma faixa curva
    if 0.30 < y < 0.42 and abs(x) < 0.45 and y > 0.30 + 0.25 * x * x:
        return DARK + (255,)
    return RED + (255,)


def zone(x, y):
    r = math.hypot(x, y)
    if 0.72 < r < 0.95:
        return (RED if r < 0.90 else DARK) + (255,)
    for cx, cy in ((0.0, -0.32), (-0.30, 0.20), (0.30, 0.20)):
        d = math.hypot(x - cx, y - cy)
        if d < 0.17:
            return (RED if d < 0.14 else DARK) + (255,)
    return None


def render(shape):
    pixels = bytearray()
    n = SUPERSAMPLE * SUPERSAMPLE
    for py in range(SIZE):
        pixels.append(0)  # filtro PNG "None" no começo de cada linha
        for px in range(SIZE):
            acc = [0, 0, 0, 0]
            for sy in range(SUPERSAMPLE):
                for sx in range(SUPERSAMPLE):
                    x = ((px + (sx + 0.5) / SUPERSAMPLE) / SIZE) * 2.0 - 1.0
                    y = ((py + (sy + 0.5) / SUPERSAMPLE) / SIZE) * 2.0 - 1.0
                    c = shape(x, y)
                    if c:
                        for i in range(3):
                            acc[i] += c[i] * c[3]
                        acc[3] += c[3]
            a = acc[3] / n
            if acc[3]:
                rgb = [round(acc[i] / acc[3]) for i in range(3)]
            else:
                rgb = [0, 0, 0]
            pixels.extend(rgb + [round(a)])
    return bytes(pixels)


def write_png(path, raw):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, shape in (("T_FieldEnemyIcon", enemy), ("T_EncounterZoneIcon", zone)):
        path = OUT_DIR / (name + ".png")
        write_png(path, render(shape))
        print("[icons] " + str(path))


if __name__ == "__main__":
    main()
