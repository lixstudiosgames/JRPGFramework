"""
ps1_monster_sounds.py — extrai os sons de batalha dos monstros do disco, organizados e já com o
mapa de em que clip/quadro cada um toca (para virar AnimNotify no Unreal).

    python Extras/ps1_monster_sounds.py --extracted <LegaiaRE/extracted> --asset-exe <asset.exe> \
        --out <pasta> (--ids 10,16 | --all)

Saída:
  <out>/Enemies/<Nome>/SW_<Nome>_<Uso>.wav   os sons do próprio monstro (o banco dele)
  <out>/Enemies/<Nome>/sounds.json           os sons dele e TODOS os notifies dos clips dele
                                             (os próprios e os comuns), por clip e quadro
  <out>/Battle/Common/SW_Battle_Cue<NN>.wav  sons da batalha que vários monstros tocam (o banco
                                             de batalha), um arquivo por som, usado por todos
  <out>/Battle/Common/common.json            cada som comum: de onde vem e quem usa

Entradas (todas do legaia-extract; nada disso entra no repositório — ARCHITECTURE.md regra 48):
  PROT/0867_battle_data.BIN  o arquivo de monstros: cada ação tem a sua trilha de sons
                             (até 8 pares "no quadro X, toque o som Y", em entry +0x54)
  PROT/0888_sound_data2.BIN  bse.dat: som do monstro -> linha (programa, tom, nota, vozes)
  PROT/0891_level_up.BIN     monster.snd: 206 bancos, o do monstro é id - 1
  SCUS_942.54                a tabela estática dos sons comuns (0x8006F198, 100 linhas)
  PROT/0868, 0869, 0876, 0889  os bancos que a categoria da linha estática escolhe

Como o jogo resolve um som da trilha (docs do RE: audio.md "animation cue track", bse-dat.md,
sfx-table.md, vab.md):
  - som >= 0x64 -> bse.dat linha som - 100; 0x1B..0x47 -> linha som + 129. Tocam no banco DO
    MONSTRO.
  - som < 0x1B -> tabela estática linha som - 1; 0x48..0x63 -> linha som. A categoria da linha
    escolhe o banco (2 = batalha/PROT 0869, 0 = sistema/0868, 6 = field/0876, 11 = recompensa/0889).
    São os sons COMUNS.
  - a linha diz programa p, tom t, nota l e quantas vozes n: toca os tons t..t+n-1 do programa p
    (juntos, em camada), na nota l. A amostra toca a 44100 Hz na nota central do tom; na nota l a
    taxa é 44100 * 2^((l - central - fino/128) / 12). Sai em 44.1 kHz pela interpolação gaussiana
    de 4 pontos do SPU (a tabela de 512 coeficientes publicada no psx-spx/no$psx — documentação do
    hardware, não bytes da Sony). Linear deixa abafado; repetir a amostra (nearest) chia nos sons
    tocados muito abaixo do tom (o golpe 0x4C da party toca a 0,14x).
  - cada tom entra no volume que o key-on do jogo dá a ele: master do banco x volume do programa x
    volume do tom (/127 cada; de 0,2 a 0,9). Sem isso as amostras saem no talo, ~2x mais alto.
  - os corpos dos sons começam 4 bytes depois do que o `vab extract` do RE assume (há um cabeçalho
    de chunk no meio). No lugar errado, ~95% dos blocos ADPCM são inválidos.

Nome dos sons do monstro: pelo clip que mais usa o som (Attack #1/#2 contam como Attack); empate, o
primeiro. Dois sons com o mesmo clip ganham _F<quadro>. Som do banco que nenhuma ação toca vira
_Unused<n>. Os comuns ficam com o número (SW_Battle_Cue15) até alguém ouvir e dar nome.

Tempo no clip do Unreal: cada quadro do PS1 são 2 quadros a 30 fps (Extras/ps1_glb_to_fbx.py), então
o quadro N toca em N / 15 s.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

OUT_RATE = 44100
CLIP_FPS = 15.0
ADPCM_FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
STATIC_TABLE_VA = 0x8006F198
STATIC_TABLE_FILE = 0x5F998          # o mesmo endereço no arquivo SCUS_942.54
STATIC_ROWS = 100
CATEGORY_BANKS = {0: "0868", 2: "0869", 6: "0876", 11: "0889"}

# Interpolação gaussiana de 4 pontos do SPU: o índice é o byte 4..11 do contador de passo (em
# 1/0x1000 de amostra) e a saída mistura as quatro últimas amostras (psx-spx, "4-Point Gaussian
# Interpolation"; a mesma tabela do legend-of-legaia-re, engine-audio/src/spu/gauss.rs).
GAUSS = [
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
]
PITCH_MAX = 0x3FFF                   # o SPU não passa de 4x


# --- VAB --------------------------------------------------------------------------------------

def spu_resample(pcm, step):
    """Toca as amostras no passo dado como o SPU: contador em 1/0x1000 de amostra, passo truncado
    no máximo do hardware, cada saída a interpolação gaussiana das quatro amostras até a atual."""
    pitch = max(1, min(PITCH_MAX, int(round(step * 0x1000))))
    out, counter, n = [], 0, len(pcm)
    while (counter >> 12) < n:
        pos, i = counter >> 12, (counter >> 4) & 0xFF
        oldest, older, old, new = (pcm[pos - k] if pos - k >= 0 else 0 for k in (3, 2, 1, 0))
        out.append(((GAUSS[0xFF - i] * oldest) >> 15) + ((GAUSS[0x1FF - i] * older) >> 15)
                   + ((GAUSS[0x100 + i] * old) >> 15) + ((GAUSS[i] * new) >> 15))
        counter += pitch
    return out


def decode_vag(data, off, size):
    out, s1, s2 = [], 0, 0
    for i in range(0, size, 16):
        b0, flags = data[off + i], data[off + i + 1]
        f, sh = min(b0 >> 4, 4), b0 & 0xF
        for j in range(14):
            byte = data[off + i + 2 + j]
            for nib in (byte & 0xF, byte >> 4):
                v = nib - 16 if nib >= 8 else nib
                v = (v << 12) >> sh
                v += (s1 * ADPCM_FILTERS[f][0] + s2 * ADPCM_FILTERS[f][1] + 32) >> 6
                v = max(-32768, min(32767, v))
                s2, s1 = s1, v
                out.append(v)
        if flags & 1:
            break
    return out


class Vab:
    """Um VAB no formato de dois chunks do Legaia: [chunk0][VabHdr..tabela de VAGs][chunk1][corpos]."""

    def __init__(self, data, hdr):
        if data[hdr:hdr + 4] != b"pBAV":
            raise ValueError("sem VAB em 0x%X" % hdr)
        self.data = data
        programs = struct.unpack_from("<H", data, hdr + 0x12)[0]
        nvags = struct.unpack_from("<H", data, hdr + 0x16)[0]
        vag_table = hdr + 0x20 + 0x800 + 0x200 * programs
        self.sizes = [struct.unpack_from("<H", data, vag_table + 2 * i)[0] << 3 for i in range(1, nvags + 1)]
        body = vag_table + 0x200 + 4                       # pula o cabeçalho do chunk 1
        self.offsets = []
        for s in self.sizes:
            self.offsets.append(body)
            body += s
        # Tons por programa, na ordem dos slots: a linha de som indexa "tom t do programa p".
        # Volume de cada tom como o key-on do jogo (FUN_80067550, docs do RE: audio.md):
        # master do banco (hdr+0x18) x mvol do programa (ProgAtr +1) x vol do tom (VagAtr +2), /127 cada
        bank_mvol = data[hdr + 0x18]
        self.by_program = {}
        for block in range(programs):
            for slot in range(16):
                o = hdr + 0x20 + 0x800 + block * 0x200 + slot * 0x20
                vag = struct.unpack_from("<h", data, o + 0x16)[0]
                if vag <= 0:
                    continue
                prog = struct.unpack_from("<h", data, o + 0x14)[0]
                prog_mvol = data[hdr + 0x20 + 16 * prog + 1] if 0 <= prog < 128 else 127
                self.by_program.setdefault(prog, []).append({
                    "center": data[o + 4], "fine": data[o + 5], "min": data[o + 6], "max": data[o + 7], "vag": vag,
                    "gain": bank_mvol * prog_mvol * data[o + 2] / 127.0 ** 3})
        self._check_alignment()

    def _check_alignment(self):
        bad = total = 0
        for off, size in zip(self.offsets, self.sizes):
            for i in range(0, min(size, 160), 16):
                b0 = self.data[off + i]
                total += 1
                bad += (b0 >> 4) > 4 or (b0 & 0xF) > 12 or self.data[off + i + 1] > 7
        if total and bad * 10 > total:
            raise ValueError("corpos dos sons desalinhados (%d de %d blocos inválidos)" % (bad, total))

    def tone(self, program, index):
        tones = self.by_program.get(program, [])
        return tones[index] if index < len(tones) else None

    def render(self, program, tone_index, note, voices=1):
        """As vozes tone_index..tone_index+voices-1 do programa, juntas, na nota dada, cada uma no
        volume do tom (o key-on velocity da trilha fica de fora: cheio)."""
        layers = []
        for i in range(max(voices, 1)):
            t = self.tone(program, tone_index + i)
            if not t:
                continue
            rate = 44100.0 * 2 ** ((note - t["center"] - t["fine"] / 128.0) / 12.0)
            pcm = decode_vag(self.data, self.offsets[t["vag"] - 1], self.sizes[t["vag"] - 1])
            layers.append((t["gain"], spu_resample(pcm, rate / OUT_RATE)))
        if not layers:
            return []
        out = [0.0] * max(len(l) for _, l in layers)
        for gain, layer in layers:
            for k, v in enumerate(layer):
                out[k] += v * gain
        return [max(-32768, min(32767, int(round(v)))) for v in out]


def write_wav(path, pcm):
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(OUT_RATE)
    w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
    w.close()


# --- tabelas de som ---------------------------------------------------------------------------

def read_rows(data, offset, count=None):
    rows = []
    o = offset
    while o + 8 <= len(data) and (count is None or len(rows) < count):
        if count is None and data[o:o + 4] == b"\0\0\0\0":
            break
        p, t, l, n, cat = data[o:o + 5]
        rows.append({"program": p, "tone": t, "note": l, "voices": n & 0x1F, "sustained": bool(n & 0x20),
                     "category": cat})
        o += 8
    return rows


def resolve_cue(cue, bse, static):
    """('monster', linha) | ('common', linha, número da linha estática) | None."""
    if cue >= 0x64:
        row = cue - 100
        return ("monster", bse[row]) if row < len(bse) else None
    if 0x1B <= cue < 0x48:
        row = cue + 129
        return ("monster", bse[row]) if row < len(bse) else None
    row = cue - 1 if cue < 0x1B else cue
    return ("common", static[row], row) if 0 <= row < len(static) else None


# --- monstro ----------------------------------------------------------------------------------

def dump_block(asset_exe, archive, monster_id):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "block.bin")
        subprocess.run([asset_exe, "monster-archive", archive, "--id", str(monster_id), "--dump-block", out],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return open(out, "rb").read() if os.path.isfile(out) else None


# Ids que repetem o nome de um id menor (formas de chefe, revanches, o Evil Bat que o Songi chama):
# levam _<id> no fim para não cair na pasta do primeiro. Songi 76 / 136 / 179, Tetsu 79 / 175,
# Evil Bat 140 / 176-178, Gaza 165 / 166, Cort 180 / 181, 183-186.
REPEATED_NAME_IDS = {136, 166, 175, 176, 177, 178, 179, 181, 183, 184, 185, 186}


def monster_name(block, monster_id):
    off = struct.unpack_from("<I", block, 0)[0]
    raw = block[off:block.index(b"\0", off)].decode("ascii", "replace")
    raw = re.sub(r"^\^.\s*", "", raw)                   # tira o escape de cor "^A "
    raw = re.sub(r"\s*\$(\d+)$", r" Lv\1", raw)          # "Gimard $2" -> "Gimard Lv2"
    name = re.sub(r"[^A-Za-z0-9]+", "_", raw).strip("_")
    return "%s_%d" % (name, monster_id) if monster_id in REPEATED_NAME_IDS else name


def action_entries(block):
    count = block[0x4A]
    return [block[e:e + 0x80] for e in struct.unpack_from("<%dI" % count, block, 0x4C)]


def action_labels(entries):
    """Os mesmos nomes do exportador .glb do RE (monster_archive::action_labels)."""
    base = []
    for i, e in enumerate(entries):
        if i == 0:
            base.append("Idle")
            continue
        act = e[0]
        base.append({1: "Move", 2: "Damaged", 4: "Knocked Down", 5: "Getting Up", 0x0B: "Block"}.get(
            act, "Attack" if 0x0D <= act <= 0x0F else "Action 0x%02X" % act))
    totals = {b: base.count(b) for b in base}
    seen, out = {}, []
    for b in base:
        if totals[b] > 1:
            seen[b] = seen.get(b, 0) + 1
            out.append("%s #%d" % (b, seen[b]))
        else:
            out.append(b)
    return out


def clip_suffix(label):
    """Igual ao Extras/ps1_glb_to_fbx.py: 'Attack #1' -> Attack1, 'Knocked Down' -> KnockedDown,
    'Action 0x13 #1' -> Action_0x13_1."""
    s = re.sub(r"(Action\s+0x[0-9A-Fa-f]+)\s+#", r"\1_", label.split(" / ")[0]).replace("#", "")
    s = re.sub(r"Action\s+0x", "Action_0x", s)
    return re.sub(r"[^A-Za-z0-9_]", "", s)


def family(label):
    return re.sub(r"\s*#\d+$", "", label)


def cue_track(entry):
    cues = []
    for k in range(8):
        frame, cue = struct.unpack_from("<HH", entry, 0x54 + 4 * k)
        if cue == 0:
            break
        cues.append((frame, cue))
    return cues


def common_file(static_row):
    return "SW_Battle_Cue%02X" % (static_row + 1 if static_row < 0x1A else static_row)


def extract_monster(monster_id, block, ctx, out_root, common_uses):
    name = monster_name(block, monster_id)
    entries = action_entries(block)
    labels = action_labels(entries)
    snd = ctx["monster_snd"]
    bank = struct.unpack_from("<I", snd, 8 + 4 * (monster_id - 1))[0] << 11
    vab = Vab(snd, bank + 4)

    own = {}            # (program, tone, note, voices) -> [(label, frame, sustained)]
    notifies = []       # (label, frame, kind, key)
    for entry, label in zip(entries, labels):
        for frame, cue in cue_track(entry):
            res = resolve_cue(cue, ctx["bse"], ctx["static"])
            if res is None:
                print("  [!] %s %s: som 0x%X sem linha" % (name, label, cue))
                continue
            if res[0] == "monster":
                r = res[1]
                key = (r["program"], r["tone"], r["note"], r["voices"])
                own.setdefault(key, []).append((label, frame, r["sustained"]))
                notifies.append((label, frame, "own", key))
            else:
                static_row = res[2]
                common_uses.setdefault(static_row, []).append({"enemy": name, "clip": "AS_%s_%s" % (name, clip_suffix(label)),
                                                               "frame": frame})
                notifies.append((label, frame, "common", static_row))

    # Nome de cada som próprio pelo uso
    named = []
    for key, users in sorted(own.items(), key=lambda kv: (kv[0][2], kv[0][1])):
        counts = {}
        for label, _, _ in users:
            counts[family(label)] = counts.get(family(label), 0) + 1
        best = max(counts.values())
        primary = next(family(l) for l, _, _ in users if counts[family(l)] == best)
        named.append([key, clip_suffix(primary), users])
    by_name = {}
    for item in named:
        by_name.setdefault(item[1], []).append(item)
    for base, items in by_name.items():
        if len(items) > 1:
            for item in items:
                frames = [f for l, f, _ in item[2] if clip_suffix(family(l)) == base]
                item[1] = "%s_F%d" % (base, min(frames))

    folder = os.path.join(out_root, "Enemies", name)
    os.makedirs(folder, exist_ok=True)
    manifest = {"monster_id": monster_id, "name": name, "bank": monster_id - 1,
                "folder": "Enemies/%s" % name, "sounds": [], "notifies": []}
    file_of, used_tones = {}, set()
    for key, role, users in named:
        program, tone, note, voices = key
        pcm = vab.render(program, tone, note, voices)
        if not pcm:
            print("  [!] %s: programa %d tom %d não existe no banco" % (name, program, tone))
            continue
        fname = "SW_%s_%s" % (name, role)
        write_wav(os.path.join(folder, fname + ".wav"), pcm)
        file_of[key] = fname
        used_tones.update((program, tone + i) for i in range(max(voices, 1)))
        manifest["sounds"].append({"sound": fname, "program": program, "tone": tone, "note": note, "voices": voices,
                                   "seconds": round(len(pcm) / OUT_RATE, 3), "loop": any(s for _, _, s in users)})
    for program, tones in sorted(vab.by_program.items()):
        for i, t in enumerate(tones):
            if (program, i) in used_tones:
                continue
            fname = "SW_%s_Unused%d" % (name, len([s for s in manifest["sounds"] if "Unused" in s["sound"]]))
            pcm = vab.render(program, i, t["min"])
            write_wav(os.path.join(folder, fname + ".wav"), pcm)
            manifest["sounds"].append({"sound": fname, "program": program, "tone": i, "note": t["min"], "voices": 1,
                                       "seconds": round(len(pcm) / OUT_RATE, 3), "loop": False})

    for label, frame, kind, key in notifies:
        if kind == "own":
            if key not in file_of:
                continue
            sound, where, loop = file_of[key], "Enemies/%s" % name, \
                next(s["loop"] for s in manifest["sounds"] if s["sound"] == file_of[key])
        else:
            sound, where, loop = common_file(key), "Battle/Common", ctx["static"][key]["sustained"]
        manifest["notifies"].append({"clip": "AS_%s_%s" % (name, clip_suffix(label)), "frame": frame,
                                     "time": round(frame / CLIP_FPS, 3), "sound": sound, "folder": where,
                                     "loop": loop})
    manifest["notifies"].sort(key=lambda n: (n["clip"], n["frame"]))
    with open(os.path.join(folder, "sounds.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print("%3d %-16s próprios: %s" % (monster_id, name, ", ".join(s["sound"][len("SW_%s_" % name):] for s in manifest["sounds"])))
    for n in manifest["notifies"]:
        print("      %-28s quadro %2d (%.2fs)  %s%s" % (n["clip"], n["frame"], n["time"], n["sound"],
                                                     "  [comum]" if n["folder"] == "Battle/Common" else ""))
    return manifest


def write_common(ctx, common_uses, out_root):
    folder = os.path.join(out_root, "Battle", "Common")
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, "common.json")
    catalog = {}
    if os.path.isfile(path):
        catalog = {c["sound"]: c for c in json.load(open(path, encoding="utf-8"))["sounds"]}
    for static_row, users in sorted(common_uses.items()):
        r = ctx["static"][static_row]
        fname = common_file(static_row)
        bank_id = CATEGORY_BANKS.get(r["category"])
        vab = ctx["banks"].get(bank_id)
        if not vab:
            print("  [!] %s: categoria %d sem banco conhecido" % (fname, r["category"]))
            continue
        pcm = vab.render(r["program"], r["tone"], r["note"], r["voices"])
        if not pcm:
            print("  [!] %s: programa %d tom %d não existe no banco %s" % (fname, r["program"], r["tone"], bank_id))
            continue
        write_wav(os.path.join(folder, fname + ".wav"), pcm)
        entry = catalog.get(fname, {"sound": fname, "used_by": []})
        entry.update({"static_row": static_row, "bank": "PROT %s" % bank_id, "category": r["category"],
                      "program": r["program"], "tone": r["tone"], "note": r["note"], "voices": r["voices"],
                      "loop": r["sustained"], "seconds": round(len(pcm) / OUT_RATE, 3)})
        seen = {(u["enemy"], u["clip"], u["frame"]) for u in entry["used_by"]}
        entry["used_by"] += [u for u in users if (u["enemy"], u["clip"], u["frame"]) not in seen]
        catalog[fname] = entry
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"folder": "Battle/Common", "sounds": sorted(catalog.values(), key=lambda c: c["sound"])},
                  f, indent=2, ensure_ascii=False)
    print("Battle/Common: %s" % ", ".join("%s (%d usos)" % (c["sound"], len(c["used_by"]))
                                          for c in sorted(catalog.values(), key=lambda c: c["sound"])))


def main():
    ap = argparse.ArgumentParser(prog="ps1_monster_sounds")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract")
    ap.add_argument("--asset-exe", required=True, help="asset.exe das ferramentas do RE")
    ap.add_argument("--out", required=True)
    ap.add_argument("--ids", default="", help="ids do arquivo de monstros, ex.: 10,16")
    ap.add_argument("--all", action="store_true", help="os 194 slots (os vazios são pulados)")
    args = ap.parse_args()
    args.asset_exe = os.path.abspath(args.asset_exe)   # o CreateProcess não acha caminho relativo com /

    prot = os.path.join(args.extracted, "PROT")

    def prot_file(num):
        name = next(f for f in os.listdir(prot) if f.startswith(num + "_"))
        return open(os.path.join(prot, name), "rb").read()

    scus = open(os.path.join(args.extracted, "SCUS_942.54"), "rb").read()
    ctx = {
        "bse": read_rows(prot_file("0888"), struct.unpack_from("<H", prot_file("0888"), 2)[0]),
        "static": read_rows(scus, STATIC_TABLE_FILE, STATIC_ROWS),
        "monster_snd": prot_file("0891"),
        "banks": {},
    }
    for bank_id in sorted(set(CATEGORY_BANKS.values())):
        data = prot_file(bank_id)
        try:
            ctx["banks"][bank_id] = Vab(data, data.find(b"pBAV"))
        except ValueError as e:
            print("[sounds] banco PROT %s ignorado: %s" % (bank_id, e))

    ids = range(1, 195) if args.all else [int(x) for x in args.ids.split(",") if x.strip()]
    if not ids:
        sys.exit("passe --ids ou --all")
    archive = os.path.join(prot, "0867_battle_data.BIN")
    common_uses, done = {}, 0
    for monster_id in ids:
        block = dump_block(args.asset_exe, archive, monster_id)
        if not block:
            continue
        try:
            extract_monster(monster_id, block, ctx, args.out, common_uses)
            done += 1
        except (ValueError, IndexError, struct.error) as e:
            print("%3d [!] %s" % (monster_id, e))
    write_common(ctx, common_uses, args.out)
    print("[sounds] %d monstro(s) em %s" % (done, args.out))


if __name__ == "__main__":
    main()
