"""Os sons da tela de resultados da vitória, tirados do disco do usuário (fora do repositório, regra 48).

    python Extras/ps1_results_sounds.py --extracted <LegaiaRE/extracted> --vab <vab.exe> --out <pasta>

Escreve dois WAV (44100 Hz, mono, 16 bits) em --out:
  SFX_ExpTick.wav  — o "tic" do contador: a cue 0x209 da slot machine do cassino de Sol (o pagamento contando moeda por moeda
                      para o saldo). Descritor no efect.dat da máquina (extraction PROT 1199, 8 bytes por cue a partir do u16 em
                      +2: programa 1, tom 5, nota 0x41), amostra no VAB class-2 dela (PROT 1198). RE: docs/subsystems/
                      minigame-slot-machine.md, "Sound".
  SFX_LevelUp.wav  — o jingle de level-up do original: a cue 0x50 (tabela de SFX da SCUS: programa 10, tom 0, nota 60, 2 vozes),
                      banco de recompensa da luta PROT 0889 (um programa, dois tons sobre a mesma amostra, tocados juntos).
                      RE: docs/formats/sfx-table.md, slot 11.

O tom de cada um sai da nota tocada contra a nota central do tom no VAB (e o ajuste fino "shift", em 1/128 de semitom): na
nota central a amostra toca a 44100 Hz. Depois importe os dois em /Game/Legaia/Battle/Sounds/Results/ (o default de
AJRPGBattleArena::VictoryResultsTickSound / VictoryResultsLevelUpSound).
"""
import argparse
import os
import struct
import subprocess
import wave

RATE = 44100


def vab_tones(path):
    """{(programa, tom): (nota central, shift, volume, índice 1-based da amostra)} do primeiro VAB do arquivo."""
    data = open(path, 'rb').read()
    start = data.find(b'pBAV')
    if start < 0:
        raise SystemExit('sem VAB em ' + path)
    d = data[start:]
    programs = [(p, d[0x20 + p * 16]) for p in range(128) if d[0x20 + p * 16]]
    tones = {}
    for index, (program, count) in enumerate(programs):
        for tone in range(count):
            o = 0x20 + 0x800 + (index * 16 + tone) * 32
            vol, center, shift = d[o + 2], d[o + 4], d[o + 5]
            vag = struct.unpack_from('<h', d, o + 0x16)[0]
            tones[(program, tone)] = (center, shift, vol, vag)
    return tones


ADPCM_COEF = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode_adpcm(body):
    """O ADPCM do SPU (blocos de 16 bytes: shift / filtro, flags, 28 nibbles), até o bloco com a flag de fim."""
    out = []
    s1 = s2 = 0
    for b in range(0, len(body) - 15, 16):
        shift = body[b] & 0x0F
        filt = min(body[b] >> 4, 4)
        flags = body[b + 1]
        if shift > 12:
            shift = 9
        c0, c1 = ADPCM_COEF[filt]
        for byte in body[b + 2:b + 16]:
            for nib in (byte & 0x0F, byte >> 4):
                n = nib - 16 if nib >= 8 else nib
                sample = ((n << 12) >> shift) + ((s1 * c0 + s2 * c1 + 32) >> 6)
                sample = max(-32768, min(32767, sample))
                s2, s1 = s1, sample
                out.append(sample / 32768.0)
        if flags & 1:
            break
    return out


def extract_samples(vab_exe, path):
    """As amostras do VAB, decodificadas (a 44100 Hz na nota central). As posições vêm do 'vab list' da ferramenta do RE.

    O vab.exe 1.10.0 lê cada amostra 4 bytes antes do lugar (o VAB começa no byte 4 do arquivo) e grava WAV vazios: aqui o
    corpo é lido do arquivo no alinhamento em que o último bloco de 16 bytes carrega a flag de fim (bit 0 das flags).
    """
    listing = subprocess.run([vab_exe, 'list', path], check=True, capture_output=True, text=True).stdout
    data = open(path, 'rb').read()
    samples = []
    for line in listing.splitlines():
        line = line.strip()
        if not line.startswith('vag['):
            continue
        offset = int(line.split(':')[1].split()[0], 16)
        size = int(line.split('size=')[1].split()[0])
        body = None
        for delta in (0, 4, -4):
            start = offset + delta
            last = start + size - 16
            if start >= 0 and last + 1 < len(data) and data[last + 1] & 1:
                body = data[start:start + size]
                break
        if body is None:
            raise SystemExit('sem o bloco de fim na amostra %d de %s' % (len(samples), path))
        samples.append(decode_adpcm(body))
    if not samples:
        raise SystemExit('o vab list não achou amostras em ' + path)
    return samples


def repitch(samples, ratio):
    """Toca a amostra ratio vezes mais rápido (interpolação linear), de volta a 44100."""
    out = []
    pos = 0.0
    last = len(samples) - 1
    while pos < last:
        i = int(pos)
        frac = pos - i
        out.append(samples[i] * (1.0 - frac) + samples[i + 1] * frac)
        pos += ratio
    return out


def voice(samples, note, center, shift):
    # Na nota central a amostra toca na taxa dela; cada semitom é 2^(1/12); o shift afina a central em 1/128 de semitom
    return repitch(samples, 2.0 ** ((note - center - shift / 128.0) / 12.0))


def mix(parts):
    length = max(len(p) for p, _ in parts)
    out = [0.0] * length
    for samples, gain in parts:
        for i, s in enumerate(samples):
            out[i] += s * gain
    return out


def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = 0.89 / peak
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples))
    print('escrito %s (%.3f s)' % (path, len(samples) / RATE))


def cue_descriptor(efect_path, cue):
    """[programa, tom, nota, vozes, classe] de uma cue de runtime (>= 0x200) no efect.dat de uma overlay."""
    d = open(efect_path, 'rb').read()
    table = struct.unpack_from('<H', d, 2)[0]
    o = table + (cue - 0x200) * 8
    return d[o], d[o + 1], d[o + 2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--extracted', required=True, help='a pasta extracted/ do legaia-extract (com PROT/)')
    ap.add_argument('--vab', required=True, help='o vab.exe das ferramentas do RE')
    ap.add_argument('--out', required=True, help='onde escrever os WAV (fora do repositório)')
    args = ap.parse_args()
    prot = os.path.join(args.extracted, 'PROT')
    os.makedirs(args.out, exist_ok=True)

    # O tic: a cue 0x209 da slot machine
    slot_vab = os.path.join(prot, '1198_other1.BIN')
    program, tone, note = cue_descriptor(os.path.join(prot, '1199_other1.BIN'), 0x209)
    center, shift, _, vag = vab_tones(slot_vab)[(program, tone)]
    samples = extract_samples(args.vab, slot_vab)
    print('tic: programa %d tom %d nota %d (central %d, shift %d, amostra %d)' % (program, tone, note, center, shift, vag))
    write_wav(os.path.join(args.out, 'SFX_ExpTick.wav'), voice(samples[vag - 1], note, center, shift))

    # O level-up: a cue 0x50 (programa 10, nota 60) toca os dois tons do programa juntos
    reward_vab = os.path.join(prot, '0889_sound_data2.BIN')
    samples = extract_samples(args.vab, reward_vab)
    parts = []
    for (prog, _), (center, shift, vol, vag) in sorted(vab_tones(reward_vab).items()):
        if prog == 10:
            parts.append((voice(samples[vag - 1], 60, center, shift), vol / 127.0))
    write_wav(os.path.join(args.out, 'SFX_LevelUp.wav'), mix(parts))


if __name__ == '__main__':
    main()
