"""
ps1_battle_music.py — as músicas de luta do disco do usuário em WAV, cada uma em introdução + laço (o par que o UE toca sem
emenda: Intro uma vez, Loop em laço).

    python Extras/ps1_battle_music.py --extracted <LegaiaRE/extracted> --tools <pasta do legaia-engine.exe / seq.exe> \
        --out <pasta> [ids...]

Saída: <out>/Music/Battle/BGM_<id>_<Nome>_Intro.wav e _Loop.wav (44,1 kHz estéreo), <out>/Music/Battle/music.json e
Docs/Data/DT_BattleMusic.csv (Row Struct FJRPGBattleMusicData: o nome, o sound set e os caminhos dos assets; o UE importa os
WAVs com Extras/ue_import_battle_music.py em /Game/Legaia/sounds/OST/Battle/).

De onde vem (legend-of-legaia-re: docs/subsystems/audio.md "The battle sound set picks the fight's track",
crates/engine-system/src/music_labels.rs): a música da luta é a partitura do banco de luta 0x36F + N (extração 877 + N), cópia
de uma faixa do music_01; N é o "sound set" que o script da cena guarda (op 0x35 sub-op 7, -1 = não troca). As 10 do banco:
0..9 = 2026 2027 2028 2030 2032 2031 2051 2067 2061 2073. A 9 (o tema de Wild Arms) está no banco mas nenhuma luta a usa: fica de
fora. A faixa id 2000 + i do music_01 está na extração 988 + i (i <= 67) ou
990 + i (i >= 68).

Como sai o som: o sequenciador + SPU do motor do RE (`legaia-engine pcm-trace --bgm-id`, na cena init_data, que não toca nada
dela). O laço vem da partitura: o NRPN 99 = 20 marca o começo do laço, 99 = 30 o fim (libsnd da PsyQ); o tempo em segundos
sai dos ticks e dos tempos do SEQ (`seq.exe events`). O motor não anda exatamente no relógio ideal (uns 0,5 %), então o período
e o começo são acertados no próprio PCM, onde o som se repete.

Com --ost <pasta>, a música sai do álbum da trilha (os WAVs do CD "<índice do sound test> <nome>.wav", mais completos que o
sequenciador do jogo): a partitura dá onde o laço começa e quanto dura, e o próprio áudio do álbum acerta os dois (o período pela
correlação do envelope, o começo pelo primeiro ponto que já se repete um período depois). Uma gravação não se repete amostra a
amostra, então o fim do Loop leva um crossfade de 50 ms com o que vem antes do começo dele (a emenda não estala).

Conteúdo extraído do disco é da Sony: os WAVs ficam FORA do repositório.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import legaia_toml as lt  # noqa: E402

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

# id -> nome (a tabela de faixas do RE, docs/reference/music-tracks.md), na ordem do banco de luta (sound set 0..9)
TRACKS = [
    (2026, "Battle1"),        # Battle theme 1 — encontro comum
    (2027, "Battle2"),        # Battle theme 2 — encontro comum, a variante
    (2028, "Boss1"),          # Boss theme 1 ("Assault")
    (2030, "Songi"),          # Songi battle
    (2032, "Boss2"),          # Boss theme 2 ("Apostles of the Mist")
    (2031, "Cort1"),          # Cort battle 1
    (2051, "Koru2"),          # Koru battle theme 2 ("The Hot Heat")
    (2067, "VanSaryu"),       # "Vahn-Saryu" na tabela do RE ("Tense Presence"): a luta do Van Saryu
    (2061, "Cort2"),          # Cort battle 2 / Juggernaut ("Apocalypse")
]
# O arquivo do álbum de cada faixa: o número dele é o índice do sound test (faixa id - 2000)
OST_INDEX = {2026: 26, 2027: 27, 2028: 28, 2030: 30, 2032: 32, 2031: 31, 2051: 51, 2067: 67, 2061: 61}
DISPLAY = {"Battle1": "Battle theme 1", "Battle2": "Battle theme 2", "Boss1": "Boss theme 1 (Assault)", "Songi": "Songi battle",
           "Boss2": "Boss theme 2 (Apostles of the Mist)", "Cort1": "Cort battle 1", "Koru2": "Koru battle theme 2 (The Hot Heat)",
           "VanSaryu": "Van Saryu battle (Evil Seru / Tense Presence)", "Cort2": "Cort battle 2 / Juggernaut (Apocalypse)"}
UE_DEST = "/Game/Legaia/sounds/OST/Battle"
RATE = 44100
SILENT_SCENE = "init_data"


def music_entry(extracted, bgm_id):
    i = bgm_id - 2000
    num = 988 + i if i <= 67 else 990 + i
    matches = glob.glob(os.path.join(extracted, "PROT", "%04d_*.BIN" % num))
    if not matches:
        sys.exit("não achei a extração %04d (faixa %d)" % (num, bgm_id))
    return matches[0]


def loop_seconds(tools, entry):
    """(começo, fim) do laço em segundos ideais, da partitura."""
    info = subprocess.run([os.path.join(tools, "seq.exe"), "info", entry], capture_output=True, text=True).stdout
    ppqn = int(re.search(r"ppqn:\s+(\d+)", info).group(1))
    tempo0 = int(re.search(r"tempo:\s+(\d+) us/qn", info).group(1))
    events = subprocess.run([os.path.join(tools, "seq.exe"), "events", entry], capture_output=True, text=True).stdout
    tempos = [(0, tempo0)]
    starts, end = [], None
    for line in events.splitlines():
        m = re.match(r"\s*(\d+)\s+\+\d+\s+(\S+)\s+(.*)", line)
        if not m:
            continue
        tick, rest = int(m.group(1)), m.group(3)
        t = re.search(r"tempo\s+us/qn=(\d+)", rest)
        if t:
            tempos.append((tick, int(t.group(1))))
        c = re.search(r"ctrl=99\s+val=(\d+)", rest)
        if c and c.group(1) == "20":
            starts.append(tick)
        elif c and c.group(1) == "30":
            end = tick
    if not starts or end is None:
        return None

    def seconds(tick):
        s, last_tick, last_tempo = 0.0, 0, tempo0
        for at, tempo in tempos:
            if at > tick:
                break
            s += (at - last_tick) * last_tempo / ppqn / 1e6
            last_tick, last_tempo = at, tempo
        return s + (tick - last_tick) * last_tempo / ppqn / 1e6

    # O fim do laço volta para o último começo marcado antes dele
    start = max(t for t in starts if t < end)
    return seconds(start), seconds(end)


def render(tools, extracted, bgm_id, seconds, path):
    frames = int(seconds * 60) + 60
    subprocess.run([os.path.join(tools, "legaia-engine.exe"), "pcm-trace", "--extracted-root", extracted, "--scene", SILENT_SCENE,
                    "--bgm-id", str(bgm_id), "--frames", str(frames), "--engine-wav", path],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wave.open(path)
    pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)
    w.close()
    return pcm


def refine(mono, start0, period0):
    """
    O período e o começo no relógio do motor. O sequenciador do motor anda em passos de quadro, com resto fracionário que não volta
    a zero no laço: as passadas não se repetem amostra a amostra (as notas caem até um quadro antes ou depois). Então o período sai
    da correlação do envelope (a 1 ms) numa janela longa, e o começo é o da partitura na mesma escala (a razão período / período da
    partitura). Devolve (começo, período, correlação 0..1).
    """
    hop = RATE // 1000
    env = np.abs(mono[:len(mono) // hop * hop]).reshape(-1, hop).mean(axis=1)
    env = np.convolve(env, np.ones(5) / 5.0, mode="same")          # 5 ms
    win = 10000                                                     # 10 s
    probe = int(start0 * 1000) + 3000
    ref = env[probe:probe + win] - env[probe:probe + win].mean()
    best, score = None, -1.0
    for p in range(int(period0 * 1000 * 0.98), int(period0 * 1000 * 1.02)):
        seg = env[probe + p:probe + p + win]
        if len(seg) < win:
            break
        seg = seg - seg.mean()
        c = float(np.dot(ref, seg) / (np.linalg.norm(ref) * np.linalg.norm(seg) + 1e-9))
        if c > score:
            best, score = p, c
    # ms -> amostras pela taxa (o passo do envelope é 44 amostras, não 44,1: multiplicar por ele encurtava o laço 0,23 %)
    period = int(round(best * RATE / 1000.0))
    start = int(round(start0 * RATE * period / (period0 * RATE)))
    return start, period, score


def read_ost(folder, index):
    """O WAV do álbum (PCM 16 ou float 32, estéreo), em int16 a 44,1 kHz."""
    names = [f for f in os.listdir(folder) if f.lower().endswith(".wav") and f.split(" ")[0] == "%02d" % index]
    if not names:
        return None, None
    data = open(os.path.join(folder, names[0]), "rb").read()
    i, fmt, nch, rate, bits = 12, None, 2, RATE, 16
    while i + 8 <= len(data):
        cid, size = data[i:i + 4], struct_unpack("<I", data, i + 4)[0]
        if cid == b"fmt ":
            fmt, nch, rate = struct_unpack("<HHI", data, i + 8)
            bits = struct_unpack("<H", data, i + 8 + 14)[0]
        elif cid == b"data":
            body = data[i + 8:i + 8 + size]
            if fmt == 3 and bits == 32:
                pcm = np.frombuffer(body, dtype=np.float32).reshape(-1, nch)
                pcm = np.clip(pcm * 32767.0, -32768, 32767).astype(np.int16)
            elif fmt == 1 and bits == 16:
                pcm = np.frombuffer(body, dtype=np.int16).reshape(-1, nch)
            else:
                sys.exit("%s: formato %s / %d bits não suportado" % (names[0], fmt, bits))
            if rate != RATE or nch != 2:
                sys.exit("%s: %d Hz / %d canais (esperado 44100 estéreo)" % (names[0], rate, nch))
            return pcm, names[0]
        i += 8 + size + (size & 1)
    return None, None


def struct_unpack(fmt, data, offset):
    import struct
    return struct.unpack_from(fmt, data, offset)


def ost_loop(pcm, start0, period0):
    """(começo, período) do laço no álbum, em amostras, perto dos da partitura (start0, period0 em segundos)."""
    mono = pcm.astype(np.float32).mean(axis=1)
    hop = RATE // 1000
    env = np.abs(mono[:len(mono) // hop * hop]).reshape(-1, hop).mean(axis=1)
    env = np.convolve(env, np.ones(5) / 5.0, mode="same")

    def corr(a, b):
        a, b = a - a.mean(), b - b.mean()
        return float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))

    win = 8000
    probe = int(start0 * 1000) + 3000
    # O álbum anda no máximo uns 0,4 % fora do relógio da partitura; uma janela larga pega um refrão repetido dentro do laço
    lo, hi = int(period0 * 1000 * 0.99), int(period0 * 1000 * 1.01)
    period = max((p for p in range(lo, hi) if probe + p + win < len(env)),
                 key=lambda p: corr(env[probe:probe + win], env[probe + p:probe + p + win]))
    # O começo: o primeiro ponto (de 2 s antes a 2 s depois do da partitura) que se repete um período depois tão bem quanto o
    # trecho de dentro do laço (85 % da correlação de lá), e continua repetindo pelo segundo seguinte
    def at(ms, length=1000):
        return corr(env[ms:ms + length], env[ms + period:ms + period + length]) if ms >= 0 and ms + period + length < len(env) else -1.0
    plateau = float(np.median([at(probe + k * 500) for k in range(10)]))
    guess = int(start0 * 1000 * period / (period0 * 1000))
    start = guess
    for ms in range(max(guess - 2000, 0), guess + 2000, 10):
        if all(at(ms + k * 250) >= 0.85 * plateau for k in range(5)):
            start = ms
            break
    return int(round(start * RATE / 1000.0)), int(round(period * RATE / 1000.0)), plateau


def crossfade_loop(pcm, start, period, fade=RATE // 20):
    """O Loop [start, start + period) com o fim misturado ao que vem antes do começo (a emenda volta para start sem estalo)."""
    loop = pcm[start:start + period].astype(np.float32)
    fade = min(fade, start, period // 4)
    if fade > 0:
        w = np.linspace(0.0, 1.0, fade, dtype=np.float32)[:, None]
        tail = loop[-fade:]
        lead = pcm[start - fade:start].astype(np.float32)
        loop[-fade:] = tail * (1.0 - w) + lead * w
    return np.clip(loop, -32768, 32767).astype(np.int16)


def write(path, pcm):
    w = wave.open(path, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(np.ascontiguousarray(pcm).tobytes())
    w.close()


def main():
    ap = argparse.ArgumentParser(prog="ps1_battle_music")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract")
    ap.add_argument("--tools", required=True, help="a pasta do legaia-engine.exe e do seq.exe")
    ap.add_argument("--out", required=True)
    ap.add_argument("--no-csv", action="store_true", help="não escreve o DT_BattleMusic.csv")
    ap.add_argument("--ost", help="a pasta do álbum da trilha (\"26 Battle Theme.wav\"...): a música sai dela, não do sequenciador")
    ap.add_argument("ids", nargs="*", type=int, help="só estas faixas (padrão: as 9 do banco de luta que as lutas usam)")
    args = ap.parse_args()

    folder = os.path.join(args.out, "Music", "BattleOST" if args.ost else "Battle")
    os.makedirs(folder, exist_ok=True)
    manifest = []
    for sound_set, (bgm_id, name) in enumerate(TRACKS):
        if args.ids and bgm_id not in args.ids:
            continue
        entry = music_entry(args.extracted, bgm_id)
        loop = loop_seconds(args.tools, entry)
        if not loop:
            print("[music] %d %s: sem marcas de laço no SEQ, fica de fora" % (bgm_id, name))
            continue
        start0, end0 = loop
        if args.ost:
            pcm, source = read_ost(args.ost, OST_INDEX[bgm_id])
            if pcm is None:
                print("[music] %d %s: sem o arquivo %02d no álbum, fica de fora" % (bgm_id, name, OST_INDEX[bgm_id]))
                continue
            start, period, err = ost_loop(pcm, start0, end0 - start0)
            base = "BGM_OST_%d_%s" % (bgm_id, name)
            loop_pcm = crossfade_loop(pcm, start, period)
            print("[music]   do álbum: %s" % source)
        else:
            with tempfile.TemporaryDirectory() as tmp:
                pcm = render(args.tools, args.extracted, bgm_id, start0 + 2 * (end0 - start0) + 15, os.path.join(tmp, "full.wav"))
            mono = pcm.astype(np.float32).mean(axis=1)
            start, period, err = refine(mono, start0, end0 - start0)
            base = "BGM_%d_%s" % (bgm_id, name)
            loop_pcm = pcm[start:start + period]
        write(os.path.join(folder, base + "_Intro.wav"), pcm[:start])
        write(os.path.join(folder, base + "_Loop.wav"), loop_pcm)
        manifest.append({"id": bgm_id, "name": name, "sound_set": sound_set, "intro": base + "_Intro", "loop": base + "_Loop",
                         "intro_seconds": round(start / RATE, 3), "loop_seconds": round(period / RATE, 3),
                         "seq_loop": [round(start0, 3), round(end0, 3)], "correlation": round(float(err), 4)})
        print("[music] %d %-10s set %d: intro %6.2f s, laço %6.2f s (partitura %.2f..%.2f, correlação %.3f)"
              % (bgm_id, name, sound_set, start / RATE, period / RATE, start0, end0, err))
    with open(os.path.join(folder, "music.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    if args.no_csv or args.ids:
        return

    def asset(name):
        return "%s/%s.%s" % (UE_DEST, name, name)
    rows = [{"---": t["name"], "DisplayName": DISPLAY[t["name"]], "SoundSet": str(t["sound_set"]), "BgmId": str(t["id"]),
             "Intro": asset(t["intro"]), "Loop": asset(t["loop"])} for t in manifest]
    bak = lt.backup("DT_BattleMusic")
    if bak:
        print("[music] backup: " + bak)
    lt.write_rows("DT_BattleMusic", ["DisplayName", "SoundSet", "BgmId", "Intro", "Loop"], rows)
    print("[music] OK: %s (%d linhas)" % (lt.csv_path("DT_BattleMusic"), len(rows)))


if __name__ == "__main__":
    main()
