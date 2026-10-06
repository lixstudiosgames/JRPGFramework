"""
Gera a textura de ruído da JRPGMist (T_JRPGMistNoise).

    python Extras/generate_mist_noise.py

Saída: Extras/Mist/T_JRPGMistNoise.png — 256x256, tileável nas duas direções.
  R = fBm de Perlin (forma das faixas)
  G = fBm de Perlin com outra seed (campo de warp)

Importar no Unreal em Content/World/Mist/ com:
  sRGB desligado, Compression = Masks (no sRGB), Address X/Y = Wrap, Mip Gen = NoMipmaps
  (o shader sempre amostra o mip 0).

Só biblioteca padrão. Mesma seed = mesmo arquivo, byte a byte.
"""

import math
import random
import struct
import sys
import zlib
from pathlib import Path

# Console do Windows em cp1252 estraga os acentos das mensagens
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

SIZE = 256
BASE_PERIOD = 4      # células de gradiente na oitava mais grossa
OCTAVES = 5          # cada oitava dobra o período, então continua tileável
PERSISTENCE = 0.5
SEEDS = (1998, 2026)  # R, G

OUT = Path(__file__).resolve().parent / "Mist" / "T_JRPGMistNoise.png"


def fade(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def make_gradients(period, rng):
    grads = []
    for _ in range(period * period):
        a = rng.random() * 2 * math.pi
        grads.append((math.cos(a), math.sin(a)))
    return grads


def perlin(x, y, period, grads):
    """Perlin 2D periódico em [0, period)."""
    x0, y0 = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - x0, y - y0

    def dot(ix, iy, dx, dy):
        gx, gy = grads[(iy % period) * period + (ix % period)]
        return gx * dx + gy * dy

    n00 = dot(x0, y0, fx, fy)
    n10 = dot(x0 + 1, y0, fx - 1, fy)
    n01 = dot(x0, y0 + 1, fx, fy - 1)
    n11 = dot(x0 + 1, y0 + 1, fx - 1, fy - 1)
    u, v = fade(fx), fade(fy)
    nx0 = n00 + u * (n10 - n00)
    nx1 = n01 + u * (n11 - n01)
    return nx0 + v * (nx1 - nx0)


def fbm_channel(seed):
    rng = random.Random(seed)
    octaves = []
    period = BASE_PERIOD
    for _ in range(OCTAVES):
        octaves.append((period, make_gradients(period, rng)))
        period *= 2

    values = []
    for py in range(SIZE):
        for px in range(SIZE):
            total, amp = 0.0, 1.0
            for period, grads in octaves:
                total += amp * perlin(px * period / SIZE, py * period / SIZE, period, grads)
                amp *= PERSISTENCE
            values.append(total)

    lo, hi = min(values), max(values)
    return [int(round(255 * (v - lo) / (hi - lo))) for v in values]


def write_png(path, rgba):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)  # filtro None
        raw.extend(rgba[y * SIZE * 4:(y + 1) * SIZE * 4])

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main():
    r = fbm_channel(SEEDS[0])
    g = fbm_channel(SEEDS[1])
    rgba = bytearray()
    for i in range(SIZE * SIZE):
        rgba.extend((r[i], g[i], 0, 255))
    write_png(OUT, rgba)
    print(f"{OUT.relative_to(OUT.parents[2])}  ({SIZE}x{SIZE}, tileável)")


if __name__ == "__main__":
    main()
