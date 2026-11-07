"""
ps1_battle_voices.py — as vozes de CD-XA da luta que não estão na trilha de som dos clips: o grito das Arts, a fanfarra do
SpecialStarter e o grunhido do block, tiradas do disco do usuário. O par do Extras/ps1_party_sounds.py (que já traz as vozes
da trilha de cada clip: Spirit, o fim da Miracle, a vitória).

    python Extras/ps1_battle_voices.py --extracted <LegaiaRE/extracted> --tools <pasta do lzs-decode.exe> \
        --xa <pasta do xa.exe demux-disc-all> --out <pasta Sounds> [--dry-run] [--no-csv]

Saída:
  <out>/Party/<Nome>/Voices/SW_<Nome>_Shout<cc>.wav    os 16 canais do banco de gritos (XA2 Vahn / XA4 Noa / XA6 Gala)
  <out>/Party/<Nome>/Voices/SW_<Nome>_Fanfare<c>.wav   os 8 canais do banco de fanfarras (XA1 / XA3 / XA5, estéreo)
  <out>/Party/<Nome>/Voices/SW_<Nome>_BlockGrunt.wav   o canal dele no XA30
  <out>/Battle/Common/SW_Battle_Cue<ID>.wav            o som que a Super toca no lugar do grito (Vahn 0x51, Noa 0xAD, Gala 0x58)
  Docs/Data/DT_BattleVoices.csv  (FJRPGBattleVoiceBankData)  um herói por linha: os sons e o tempo de leitura de cada canal
  Docs/Data/DT_ArtVoices.csv     (FJRPGArtVoiceData)         uma Art por linha: os canais candidatos do grito, o par da Hyper
O UE importa com Extras/ue_import_battle_voices.py (no editor) e as duas DataTables pelo CSV.

O que o original faz (legend-of-legaia-re: docs/subsystems/battle-action-helpers.md "Battle voice cues", audio.md "Battle
arts-voice shout path"; crates/art/src/arts_voice.rs e hyper_fanfare.rs):
  - O grito: FUN_8004C140(herói, constante da Art, flag) sorteia um canal do banco de gritos, sem repetir o último. Os
    candidatos vêm de tabelas da SCUS por herói: a de faixa 0x800781A4 ([lo, hi, second_lo]); a primeira metade
    (lo <= ac <= hi, registro base + (hi - ac) * 0x0F) em três variantes (a trava ctx+0x243 ligada: 0x80077C0C...; senão
    flag != 0, outra Art depois: 0x80077ABC...; senão, a última da string: 0x80077B64...); a segunda metade (ac >= second_lo,
    registro base + (ac - second_lo) * 0x10, uma só, as finalizações das Supers): com flag 0 sorteia dela, com flag != 0
    não toca clip e manda um som no lugar. Um registro é uma lista de canais: o byte 0 sempre entra; se o byte 1 != 0, vai
    até o próximo 0.
  - Duração de cada canal: (byte da tabela 0x80077A8C + canal + herói * 0x10) * 0x3C + 99) / 100, em vsyncs (o trecho que o
    drive lê: dur * 2,5 setores a 150 setores/s = dur / 60 s). O WAV é cortado aí (ou no fim do sinal, o que vier antes).
  - A fanfarra: no commit do SpecialStarter (0x1A). Super, Miracle ou Art nova: o canal 1 do banco (0x101 / 0x111 / 0x121).
    Hyper: um cara ou coroa entre dois canais fixos por Art (as 9 linhas do switch de FUN_8004AD80, abaixo). Duração pela
    tabela u16 0x800788B8 (índice = id - 0x100), na mesma conta.
  - O grunhido do block: FUN_8003D53C(0x1D = XA30, canal, dur) pelo herói que bateu: Vahn (0, 0x26), Noa (4, 0x2E),
    Gala (6, 0x1A).
  - A trava (ctx+0x243) é o byte +0x87 do clip que o commit anterior pôs: o SpecialStarter (0x1A) e as finalizações das Supers
    a ligam. Sai daqui por herói e por Art (bSpecialStarterLatch, bRaisesLatch).

Conteúdo extraído do disco é da Sony: os WAVs ficam FORA do repositório. Os CSVs só levam números e caminhos de asset.
"""
import argparse
import csv
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import legaia_toml as lt  # noqa: E402
from ps1_monster_sounds import CATEGORY_BANKS, STATIC_ROWS, STATIC_TABLE_FILE, Vab, read_rows, write_wav  # noqa: E402
from ps1_party_sounds import CHARACTERS, PLAYER_FILES, lzs, scus_offset, write_wav_raw  # noqa: E402

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

PARTY_ROOT = "/Game/Legaia/Battle/Party"
COMMON_ROOT = "/Game/Legaia/Battle/Sounds/Common"

RANGE_TABLE_VA = 0x800781A4
FIRST_HALF = {                        # as três variantes da primeira metade, por herói
    "last": [0x80077B64, 0x80077D5C, 0x80077F54],       # trava 0, flag 0 (a Art fecha a string)
    "followed": [0x80077ABC, 0x80077CB4, 0x80077EAC],   # trava 0, flag != 0 (outra coisa vem depois)
    "latched": [0x80077C0C, 0x80077E04, 0x80077FFC],    # trava ligada
}
SECOND_HALF = [0x800780A4, 0x80078104, 0x80078154]
SECOND_HALF_RECORDS = [6, 5, 5]       # as três tabelas estão coladas: passar disso lê a do próximo herói
SHOUT_DUR_VA = 0x80077A8C
FANFARE_DUR_VA = 0x800788B8
SHOUT_XA = ["XA2", "XA4", "XA6"]
FANFARE_XA = ["XA1", "XA3", "XA5"]
SHOUT_CHANNELS, FANFARE_CHANNELS = 16, 8
GENERIC_FANFARE_CHANNEL = 1
# As 9 Hypers do switch de FUN_8004AD80 (0x8004B8D4 / 0x8004B9A0 / 0x8004BA6C): constante -> id base; o par é
# {base & 7, base & 7 + 3}. Imediatos do código, não tabela (legaia_art::hyper_fanfare::HYPER_FANFARES)
HYPER_FANFARES = {
    "Vahn": {0x1C: 0x104, 0x1D: 0x103, 0x1E: 0x102},
    "Noa": {0x1D: 0x114, 0x1F: 0x113, 0x20: 0x112},
    "Gala": {0x1C: 0x124, 0x1D: 0x123, 0x1E: 0x122},
}
BLOCK_GRUNT = {"Vahn": (0, 0x26), "Noa": (4, 0x2E), "Gala": (6, 0x1A)}   # XA30: (canal, dur), 0x801EEB44
# Com flag != 0 na segunda metade, FUN_8004C140 manda um som no lugar do clip (0x8004C5D4..0x8004C628): Vahn e Gala pelo
# dispatcher (linha da tabela estática), Noa pelo funil (>= 0xA7 num herói: bse.dat, o banco da luta)
FOLLOWED_CUE = {"Vahn": 0x51, "Noa": 0xAD, "Gala": 0x58}
SPECIAL_STARTER = 0x1A
ART_BANK_BASE = 0x10
ART_RECORD, ART_ENTRY = 0xD0, 0x24
LATCH_BYTE = 0x87
SILENCE = 200


def u8(scus, va):
    return scus[scus_offset(va)]


def members(rec):
    """Os canais de um registro, como FUN_8004C140 conta: o byte 0 sempre; se o 1 != 0, até o próximo 0."""
    if rec[1] == 0:
        out = [rec[0]]
    else:
        k = 2
        while k < len(rec) and rec[k] != 0:
            k += 1
        out = list(rec[:k])
    return out if all(c < 16 for c in out) else None


def read_pools(scus):
    """{herói: {constante: {"last": [...], "followed": [...], "latched": [...]} ou {"finisher": [...]}}}."""
    pools = {}
    for c, name in enumerate(CHARACTERS):
        lo, hi, second_lo = (u8(scus, RANGE_TABLE_VA + 3 * c + k) for k in range(3))
        table = {}
        for ac in range(lo, hi + 1):
            entry = {}
            for variant, bases in FIRST_HALF.items():
                o = scus_offset(bases[c]) + (hi - ac) * 0x0F
                ch = members(scus[o:o + 0x0F])
                if ch is None:
                    sys.exit("%s 0x%02X: registro %s com canal > 15 — a SCUS não bate com a do RE" % (name, ac, variant))
                entry[variant] = ch
            table[ac] = entry
        for i in range(SECOND_HALF_RECORDS[c]):
            ac = second_lo + i
            o = scus_offset(SECOND_HALF[c]) + 0x10 * i
            ch = members(scus[o:o + 0x10])
            if ch is None:
                sys.exit("%s 0x%02X: registro da segunda metade com canal > 15" % (name, ac))
            if ac in table:
                sys.exit("%s 0x%02X: nas duas metades (o script não sabe tocar os dois)" % (name, ac))
            table[ac] = {"finisher": ch}
        pools[name] = table
        print("[voices] %s: primeira metade 0x%02X..0x%02X, segunda 0x%02X..0x%02X"
              % (name, lo, hi, second_lo, second_lo + SECOND_HALF_RECORDS[c] - 1))
    return pools


def shout_vsyncs(scus, c, channel):
    return (u8(scus, SHOUT_DUR_VA + channel + c * 0x10) * 0x3C + 99) // 100


def fanfare_vsyncs(scus, c, channel):
    n = 2 * c * 8 + channel          # id - 0x100: o banco de fanfarras do herói c é o clip 2c
    raw = struct.unpack_from("<H", scus, scus_offset(FANFARE_DUR_VA) + 2 * n)[0]
    return (raw * 0x3C + 99) // 100


def latch_bytes(extracted, tools, name):
    """{id do banco de Arts: byte +0x87} do arquivo do herói (PROT 0863..0865, record[0] +0x58)."""
    prot = os.path.join(extracted, "PROT")
    path = os.path.join(prot, next(f for f in os.listdir(prot) if f.startswith(PLAYER_FILES[name] + "_")))
    data = open(path, "rb").read()
    budget = struct.unpack_from("<4I", data, 0)[3]
    record0 = lzs(tools, path, 0x10, budget)
    bank = struct.unpack_from("<I", record0, 0x58)[0]
    count = struct.unpack_from("<I", record0, bank)[0]
    return {ART_BANK_BASE + k: record0[bank + 4 + ART_RECORD * k + ART_ENTRY + LATCH_BYTE] for k in range(count)}


def write_xa(xa_dir, xa, channel, vsyncs, path, dry):
    """
    O canal do XA, cortado no trecho que o drive lê (vsyncs / 60 s) ou no fim do sinal + 50 ms, e sem o silêncio do começo (os
    canais abrem com ~0,15 s de silêncio, a espera do CD que o FJRPGBattleVoices já põe com VoiceLatencySeconds: deixar os dois
    atrasava a voz da animação). Fica um respiro de 10 ms antes do som. Devolve os segundos.
    """
    w = wave.open(os.path.join(xa_dir, "%s_file1_ch%d.wav" % (xa, channel)))
    nch, rate, frames = w.getnchannels(), w.getframerate(), w.getnframes()
    pcm = list(struct.unpack("<%dh" % (frames * nch), w.readframes(frames)))
    w.close()
    loud = [i for i, v in enumerate(pcm) if abs(v) > SILENCE]
    first = max((loud[0] // nch if loud else 0) - rate // 100, 0)
    last = (loud[-1] if loud else 0) // nch
    span = int(round(vsyncs / 60.0 * rate))
    end = min(frames, span, last + 1 + rate // 20)
    if not dry:
        write_wav_raw(path, pcm[first * nch:end * nch], nch, rate)
    return round((end - first) / float(rate), 3)


def asset(folder, name):
    return "%s/%s.%s" % (folder, name, name)


def art_rows():
    """{(herói, constante): chave da Art} do DT_Arts.csv (as Supers, constante 0, não têm clip próprio)."""
    out = {}
    with open(lt.csv_path("DT_Arts"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            ac = int(r["ActionConstant"] or 0)
            if ac:
                out[(r["Character"], ac)] = r["---"]
    return out


def main():
    ap = argparse.ArgumentParser(prog="ps1_battle_voices")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract")
    ap.add_argument("--tools", required=True, help="a pasta das ferramentas do RE (lzs-decode.exe)")
    ap.add_argument("--xa", required=True, help="a saída do `xa.exe demux-disc-all` (XA<n>_file1_ch<c>.wav)")
    ap.add_argument("--out", required=True, help="a pasta Sounds (a mesma do ps1_party_sounds)")
    ap.add_argument("--dry-run", action="store_true", help="não escreve nada")
    ap.add_argument("--no-csv", action="store_true", help="só os WAVs")
    args = ap.parse_args()
    dry = args.dry_run

    scus = open(os.path.join(args.extracted, "SCUS_942.54"), "rb").read()
    pools = read_pools(scus)
    arts = art_rows()

    # Os sons que a Super toca no lugar do grito (o mesmo nome dos comuns do ps1_party_sounds)
    prot = os.path.join(args.extracted, "PROT")

    def prot_path(num):
        return os.path.join(prot, next(f for f in os.listdir(prot) if f.startswith(num + "_")))

    static = read_rows(scus, STATIC_TABLE_FILE, STATIC_ROWS)
    bse_data = open(prot_path("0888"), "rb").read()
    bse = read_rows(bse_data, struct.unpack_from("<H", bse_data, 2)[0])
    banks = {}
    common_dir = os.path.join(args.out, "Battle", "Common")
    if not dry:
        os.makedirs(common_dir, exist_ok=True)

    bank_rows, voice_rows = [], []
    for c, name in enumerate(CHARACTERS):
        folder = os.path.join(args.out, "Party", name, "Voices")
        if not dry:
            os.makedirs(folder, exist_ok=True)
        dest = "%s/%s/Voices" % (PARTY_ROOT, name)

        shouts, shout_seconds = [], []
        for ch in range(SHOUT_CHANNELS):
            sound = "SW_%s_Shout%02d" % (name, ch)
            vs = shout_vsyncs(scus, c, ch)
            write_xa(args.xa, SHOUT_XA[c], ch, vs, os.path.join(folder, sound + ".wav"), dry)
            shouts.append(asset(dest, sound))
            shout_seconds.append(round(vs / 60.0, 3))
        fanfares, fanfare_seconds = [], []
        for ch in range(FANFARE_CHANNELS):
            sound = "SW_%s_Fanfare%d" % (name, ch)
            vs = fanfare_vsyncs(scus, c, ch)
            write_xa(args.xa, FANFARE_XA[c], ch, vs, os.path.join(folder, sound + ".wav"), dry)
            fanfares.append(asset(dest, sound))
            fanfare_seconds.append(round(vs / 60.0, 3))
        grunt_ch, grunt_vs = BLOCK_GRUNT[name]
        grunt = "SW_%s_BlockGrunt" % name
        write_xa(args.xa, "XA30", grunt_ch, grunt_vs, os.path.join(folder, grunt + ".wav"), dry)

        cue = FOLLOWED_CUE[name]
        cue_sound = "SW_Battle_Cue%02X" % cue
        if cue >= 0xA7:
            row, bank_id = bse[cue - 100], CATEGORY_BANKS[2]
        else:
            row = static[cue - 1 if cue < 0x48 else cue]
            bank_id = CATEGORY_BANKS[row["category"]]
        if bank_id not in banks:
            data = open(prot_path(bank_id), "rb").read()
            banks[bank_id] = Vab(data, data.find(b"pBAV"))
        if not dry:
            write_wav(os.path.join(common_dir, cue_sound + ".wav"),
                      banks[bank_id].render(row["program"], row["tone"], row["note"], row["voices"]))

        latches = latch_bytes(args.extracted, args.tools, name)
        bank_rows.append({
            "---": name, "Character": name,
            "Shouts": lt.ue_array(shouts, quote=True), "ShoutSeconds": lt.ue_array(shout_seconds),
            "Fanfares": lt.ue_array(fanfares, quote=True), "FanfareSeconds": lt.ue_array(fanfare_seconds),
            "GenericFanfareChannel": str(GENERIC_FANFARE_CHANNEL),
            "BlockGrunt": asset(dest, grunt), "BlockGruntSeconds": str(round(grunt_vs / 60.0, 3)),
            "FinisherCue": asset(COMMON_ROOT, cue_sound),
            "bSpecialStarterLatch": lt.ue_bool(latches.get(SPECIAL_STARTER, 0) != 0),
        })
        print("[voices] %s: 16 gritos (%s), 8 fanfarras (%s), grunhido %s, som da Super %s, trava do 0x1A %d"
              % (name, SHOUT_XA[c], FANFARE_XA[c], grunt, cue_sound, latches.get(SPECIAL_STARTER, 0)))

        # Uma linha por Art que grita, tem fanfarra ou liga a trava
        constants = set(pools[name]) | set(HYPER_FANFARES[name]) | {ac for ac, b in latches.items() if b and ac > SPECIAL_STARTER}
        for ac in sorted(constants):
            key = arts.get((name, ac))
            if not key:
                print("[voices]   %s 0x%02X: nenhuma Art do DT_Arts com essa constante, fica de fora" % (name, ac))
                continue
            pool = pools[name].get(ac, {})
            pair = ()
            if ac in HYPER_FANFARES[name]:
                base = (HYPER_FANFARES[name][ac] - 0x100) & 7
                pair = (base, base + 3)
            finisher = "finisher" in pool
            voice_rows.append({
                "---": key, "Character": name, "ActionConstant": str(ac),
                "Shout": lt.ue_array(pool.get("finisher", pool.get("last", []))),
                "ShoutFollowed": lt.ue_array(pool.get("followed", [])),
                "ShoutLatched": lt.ue_array(pool.get("latched", [])),
                "bFinisherTable": lt.ue_bool(finisher),
                "bRaisesLatch": lt.ue_bool(latches.get(ac, 0) != 0),
                "Fanfare": lt.ue_array(pair),
            })
            r = voice_rows[-1]
            print("[voices]   %-30s 0x%02X grito %-22s seguida %-22s trava %-22s%s%s%s" % (
                key, ac, r["Shout"], r["ShoutFollowed"], r["ShoutLatched"], " finalização" if finisher else "",
                " liga-trava" if r["bRaisesLatch"] == "True" else "", " fanfarra " + r["Fanfare"] if pair else ""))

    if dry or args.no_csv:
        print("[voices] %s" % ("dry-run: nada escrito" if dry else "CSVs não escritos (--no-csv)"))
        return
    for table, columns, rows in (
            ("DT_BattleVoices", ["Character", "Shouts", "ShoutSeconds", "Fanfares", "FanfareSeconds", "GenericFanfareChannel",
                                 "BlockGrunt", "BlockGruntSeconds", "FinisherCue", "bSpecialStarterLatch"], bank_rows),
            ("DT_ArtVoices", ["Character", "ActionConstant", "Shout", "ShoutFollowed", "ShoutLatched", "bFinisherTable",
                              "bRaisesLatch", "Fanfare"], voice_rows)):
        bak = lt.backup(table)
        if bak:
            print("[voices] backup: " + bak)
        lt.write_rows(table, columns, rows)
        print("[voices] OK: %s (%d linhas)" % (lt.csv_path(table), len(rows)))


if __name__ == "__main__":
    main()
