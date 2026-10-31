"""
ps1_party_sounds.py — os clips de batalha da party com o nome do que eles são, a velocidade do jogo e
os sons que cada um toca, tirados do disco (para virar AnimNotify no Unreal). O par do
Extras/ps1_monster_sounds.py para Vahn, Noa e Gala.

    python Extras/ps1_party_sounds.py --extracted <LegaiaRE/extracted> --tools <pasta do lzs-decode.exe> \
        --xa <pasta do xa.exe demux-disc-all> --out <pasta> Vahn [Noa Gala]

Saída (o mesmo formato do ps1_monster_sounds, que o Extras/ue_import_monster_sounds.py lê):
  <out>/Party/<Nome>/SW_<Nome>_Voice<ID>.wav   as vozes (CD-XA) e SW_<Nome>_VictoryVoice<clipe>
  <out>/Party/<Nome>/sounds.json               os clips (nome do .glb -> nome certo, RateScale) e os
                                               notifies, por clip e quadro
  <out>/Battle/Common/SW_Battle_Cue<ID>.wav     os sons comuns e os do banco de batalha que ele usa

Os clips (docs do RE: battle-data-pack.md, battle-actor-rendering.md, audio.md):
  - O arquivo do jogador (PROT 0863..0865) tem as ações no record[0] (slots 0..0xB), os golpes de
    direção nas seções de equipamento (L/R na 2/3, D/U na 4; aqui os da seção padrão, os que o
    .glb da página Arts traz) e o banco de Arts em record[0] +0x58 (registros de 0xD0, a ação em
    +0x24, id 0x10 + índice). O .glb da página Arts dá um clip por registro, com o nome interno
    (que não é o da Art: "Beatdunk" é Fire Blow) ou o id em hex. Aqui cada um ganha o nome do que é:
      slots 7 / 8 / 9          Dying (cai e agacha) / Down (morto, agachado — o "clip 8" em que um
                               herói morto fica) / UseItem (item e magia: o estado de cast põe 9)
      0x10                     Spirit
      0x11..0x18               as poses de vitória, pela tabela da SCUS 0x800788A0 (6 bytes por
                               personagem: o par saudável, o alternativo e o fraco, de HP baixo);
                               as duas que nenhum personagem usa ficam VictoryExtra
      0x19 / 0x1A              ArtStarter / SpecialStarter (o das Super e Miracle)
      0x1B..                   a Art: o id é o Action Constant (legend-of-legaia-re, art/tables.rs)
  - Chain Cut ("chain_cut" de cada clip, em s): com outra ação na fila do turno, o jogo corta o
    clip assim que o quadro passa do primeiro beat (+0x10) + 2, se a trava +0x76 estiver em 0
    (FUN_80047430). Os golpes de direção têm beat 7: emendam no quadro 10, sem a volta à guarda.
    As Arts têm a trava e tocam inteiras.
  - Velocidade: o cursor anda (frame_dt x escala x rate) >> 1 por quadro em 12.4, escala 8 = 15 x
    rate quadros/s (o mesmo dos monstros). Uma Art (>= 0x1B) roda a escala 4 e o SpecialStarter a 2
    (o jogo desacelera a luta inteira). O .glb assa tudo a 7,5 x rate, então o clip leva RateScale 2
    (normal), 1 (Art) ou 0,5 (SpecialStarter).

Os sons:
  - Toda ação tem a trilha em +0x54: até 8 pares (u16 quadro, u16 som), som 0 termina. O quadro é o
    do clip: no asset ele cai em quadro / (7,5 x rate) s (o RateScale cuida do resto). O par repetido
    em seguida não soa duas vezes (o roteador descarta o som igual ao último).
  - Como o jogo resolve o som de um herói (FUN_8004FE5C, slot < 3):
      0xC8..0xFF (menos 0xFA)  voz em CD-XA: +0x38, clip = x >> 3 (1/3/5 -> 26/27/28), canal = x & 7.
                               Vahn 0xC8..0xD7 (XA1, XA27), Noa 0xD8..0xE7 (XA3, XA28), Gala
                               0xE8..0xF7 (XA5, XA29). O último id de cada bloco é o grito, que o
                               jogo sorteia entre os canais 7 e 6: sai como _A (7) e _B (6).
      < 0x48                   linha da tabela estática som - 1 (som comum)
      0x48..0x63               linha da tabela estática som (som comum)
      >= 0xA7                  bse.dat linha som - 100, no banco de batalha (PROT 0869)
      0x64..0xA6               bse.dat no VAB slot 7. Na luta é o banco do MONSTRO (muda com o
                               inimigo, não dá para fixar num arquivo — fica de fora, avisado). Nas
                               poses de vitória (0x11..0x18) é a voz de vitória: o sequenciador de
                               resultados carrega no slot 7 o clipe do fim do monster.snd que a
                               tabela da SCUS 0x80078867 + ação + personagem x 8 dá (clipe = byte - 1),
                               e o 0xA1 da trilha da pose (linha programa 0, tom 0, nota 60) é ela.
    Os comuns usam o nome dos do ps1_monster_sounds (SW_Battle_Cue<ID>, pelo id do som), então
    um som que os monstros já trouxeram é o mesmo arquivo.

Conteúdo extraído do disco é da Sony: entradas e saídas ficam FORA do repositório.
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ps1_monster_sounds import (CATEGORY_BANKS, STATIC_ROWS, STATIC_TABLE_FILE, Vab,  # noqa: E402
                                clip_suffix, read_rows, write_wav)

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

CHARACTERS = ["Vahn", "Noa", "Gala"]                 # ordem dos arquivos do jogador e das tabelas
PLAYER_FILES = {"Vahn": "0863", "Noa": "0864", "Gala": "0865"}
# Rótulo do .glb e nome certo de cada slot do record[0]
ACTIONS = {0x0: ("Idle", "Idle"), 0x1: ("Walk / Approach", "Walk"), 0x2: ("Flinch 1", "Flinch1"),
           0x3: ("Flinch 2", "Flinch2"), 0x4: ("Knockdown", "Knockdown"), 0x5: ("Get Up", "GetUp"),
           0x7: ("Ready", "Dying"), 0x8: ("Recover", "Down"), 0x9: ("Defeat", "UseItem"),
           0xB: ("Block", "Block")}
SWINGS = ((2, ["Swing L"]), (3, ["Swing R"]), (4, ["Swing D", "Swing U"]))
ART_ANIM_ID_BASE = 0x10
ART_RECORD = 0xD0
ART_ENTRY = 0x24
SPIRIT, ART_STARTER, SPECIAL_STARTER, FIRST_ART = 0x10, 0x19, 0x1A, 0x1B
RATE_NORMAL, RATE_ART, RATE_SPECIAL_STARTER = 2.0, 1.0, 0.5
XA_CLIP_REMAP = {1: 26, 3: 27, 5: 28}
SHOUTS = {0xD7, 0xE7, 0xF7}
SILENCE = 200          # amplitude abaixo da qual o fim do canal XA é silêncio
VICTORY_POSES_VA = 0x800788A0
VICTORY_VOICES_VA = 0x80078867
VICTORY_ROLES = ["Victory1", "Victory2", "VictoryAlt1", "VictoryAlt2", "VictoryWeak1", "VictoryWeak2"]

# Action Constant 0x1B.. -> nome da Art (legend-of-legaia-re, crates/art/src/tables.rs)
ART_NAMES = {
    "Vahn": ["Vahn's Craze", "Burning Flare", "Fire Blow", "Tornado Flame (Hyper)", "Cyclone", "Hurricane",
             "PK Combo", "Spin Combo", "Pyro Pummel", "Cross-Kick", "Power Punch", "Slash Kick", "Somersault",
             "Charging Scorch", "Hyper Elbow", "Tornado Flame (Miracle)", "Tri-Somersault", "Maximum Blow",
             "Fire Tackle", "Power Slash", "Rolling Combo 1", "Rolling Combo 2"],
    "Noa": ["Noa's Ark", "Hurricane Kick 1", "Hurricane Kick 2", "Hurricane Kick 3", "Vulture Blade",
            "Frost Breath", "Tempest Break", "Rushing Gale", "Tough Love", "Swan Driver", "Bird Step",
            "Dolphin Attack", "Mirage Lancer", "Blizzard Bash", "Sonic Javelin", "Acrobatic Blitz", "Lizard Tail",
            "Jurassic Blow 1", "Jurassic Blow 2", "Triple Lizard", "Super Javelin", "Super Tempest", "Love You",
            "Dragon Fangs"],
    "Gala": ["Biron Rage", "Explosive Fist", "Lightning Storm", "Thunder Punch (Hyper)", "Bull Horns",
             "Electro Thrash", "Neo Raising", "Black Rain", "Side Kick", "Head-Splitter", "Guillotine", "Back Punch",
             "Ironhead", "Battering Ram", "Flying Knee Attack", "Thunder Punch (Miracle)", "Back Punch x3",
             "Super Ironhead", "Rushing Crush", "Heaven's Drop", "Neo Static Raising"],
}


def scus_offset(va):
    return va - 0x80010000 + 0x800


def camel(text):
    """"Vahn's Craze" -> VahnsCraze, "Tornado Flame (Hyper)" -> TornadoFlameHyper, "Cross-Kick" -> CrossKick."""
    return "".join(w[:1].upper() + w[1:] for w in re.split(r"[^A-Za-z0-9]+", text.replace("'", "")) if w)


def cue_track(buf, entry):
    cues = []
    for k in range(8):
        frame, cue = struct.unpack_from("<HH", buf, entry + 0x54 + 4 * k)
        if cue == 0:
            break
        cues.append((frame, cue))
    return cues


def lzs(tools, src, skip, size):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "out.bin")
        subprocess.run([os.path.join(tools, "lzs-decode.exe"), "raw", "--size", str(size), "--skip", str(skip),
                        "-o", out, src], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return open(out, "rb").read()


def clips_of(name, player_path, tools, scus):
    """Os clips de um herói, na ordem do .glb: [{glb, clip, id, rate_scale, rate, cues}]."""
    data = open(player_path, "rb").read()
    desc_off, _, _, budget = struct.unpack_from("<4I", data, 0)
    record0 = lzs(tools, player_path, 0x10, budget)
    clips = []

    def add(label, new, anim_id, rate_scale, buf, entry):
        clips.append({"glb": label, "clip": new, "id": anim_id, "rate_scale": rate_scale,
                      "rate": buf[entry + 0x78], "cues": cue_track(buf, entry),
                      "beat": buf[entry + 0x10], "lock": buf[entry + 0x76]})

    for slot, off in enumerate(struct.unpack_from("<12I", record0, 0)):
        if slot in ACTIONS and off:
            add(ACTIONS[slot][0], ACTIONS[slot][1], slot, RATE_NORMAL, record0, off)

    # Os golpes de direção da seção padrão (id 0) de cada seção: slots 0xC..0xF
    defaults, n = [], 0
    while True:
        item, off, size = struct.unpack_from("<3I", data, desc_off + 12 * n)
        if not (item or off or size):
            break
        if item == 0:
            defaults.append(off)
        n += 1
    slot = 0xC
    for section, labels in SWINGS:
        off = 0x8000 + defaults[section]
        payload = lzs(tools, player_path, off + 4, struct.unpack_from("<I", data, off)[0])
        for k, label in enumerate(labels):
            entry = struct.unpack_from("<I", payload, 4 + 4 * k)[0]
            if entry:
                add(label, label.replace(" ", ""), slot, RATE_NORMAL, payload, entry)
            slot += 1

    # As poses de vitória deste herói: saudável, alternativa, fraca
    who = CHARACTERS.index(name)
    poses = scus[scus_offset(VICTORY_POSES_VA) + 6 * who:scus_offset(VICTORY_POSES_VA) + 6 * who + 6]
    victory = {pose: role for pose, role in zip(poses, VICTORY_ROLES)}
    extra = 0

    # O banco de Arts: um clip por registro, com o rótulo que o .glb dá e o nome do que é
    bank = struct.unpack_from("<I", record0, 0x58)[0]
    used = [c["glb"] for c in clips]
    arts = ART_NAMES[name]
    for k in range(struct.unpack_from("<I", record0, bank)[0]):
        rec = bank + 4 + ART_RECORD * k
        anim_id = ART_ANIM_ID_BASE + k
        label = record0[rec + 0x10:rec + ART_ENTRY].split(b"\0")[0].decode("ascii", "replace") or "0x%02X" % anim_id
        if label in used:
            label = "%s (0x%02X)" % (label, anim_id)
        used.append(label)
        rate_scale = RATE_NORMAL
        if anim_id == SPIRIT:
            new = "Spirit"
        elif anim_id < ART_STARTER:
            if anim_id in victory:
                new = victory[anim_id]
            else:
                extra += 1
                new = "VictoryExtra%d" % extra
        elif anim_id == ART_STARTER:
            new = "ArtStarter"
        elif anim_id == SPECIAL_STARTER:
            new, rate_scale = "SpecialStarter", RATE_SPECIAL_STARTER
        else:
            index = anim_id - FIRST_ART
            new = camel(arts[index]) if index < len(arts) else "Art0x%02X" % anim_id
            rate_scale = RATE_ART
        add(label, new, anim_id, rate_scale, record0, rec + ART_ENTRY)
    return clips


def xa_voice(xa_dir, cue, take_b=False):
    """(arquivo XA, canal, pcm intercalado, canais, taxa) da voz do id, sem o silêncio do fim."""
    x = cue + 0x38 - 0x100
    clip = XA_CLIP_REMAP.get(x >> 3, x >> 3)
    channel = (x & 7) - (1 if take_b else 0)
    xa = "XA%d" % (clip + 1)
    w = wave.open(os.path.join(xa_dir, "%s_file1_ch%d.wav" % (xa, channel)))
    nch, rate, frames = w.getnchannels(), w.getframerate(), w.getnframes()
    pcm = list(struct.unpack("<%dh" % (frames * nch), w.readframes(frames)))
    w.close()
    last = max((i for i, v in enumerate(pcm) if abs(v) > SILENCE), default=len(pcm) - 1)
    end = min(len(pcm), (last // nch + 1 + rate // 20) * nch)        # + 50 ms de cauda
    return xa, channel, pcm[:end], nch, rate


def write_wav_raw(path, pcm, channels, rate):
    w = wave.open(path, "wb")
    w.setnchannels(channels)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))
    w.close()


def victory_clip(scus, name, anim_id):
    """O clipe do monster.snd que a pose de vitória anim_id do herói carrega no VAB slot 7."""
    return scus[scus_offset(VICTORY_VOICES_VA) + anim_id + (CHARACTERS.index(name) + 1) * 8] - 1


def victory_bank(monster_snd, clip):
    return Vab(monster_snd, (struct.unpack_from("<I", monster_snd, 8 + 4 * clip)[0] << 11) + 4)


def main():
    ap = argparse.ArgumentParser(prog="ps1_party_sounds")
    ap.add_argument("--extracted", required=True, help="a pasta extracted/ do legaia-extract")
    ap.add_argument("--tools", required=True, help="a pasta das ferramentas do RE (lzs-decode.exe)")
    ap.add_argument("--xa", required=True, help="a saída do `xa.exe demux-disc-all` (XA<n>_file1_ch<c>.wav)")
    ap.add_argument("--out", required=True)
    ap.add_argument("names", nargs="+", choices=CHARACTERS)
    args = ap.parse_args()

    prot = os.path.join(args.extracted, "PROT")

    def prot_path(num):
        return os.path.join(prot, next(f for f in os.listdir(prot) if f.startswith(num + "_")))

    scus = open(os.path.join(args.extracted, "SCUS_942.54"), "rb").read()
    bse_data = open(prot_path("0888"), "rb").read()
    bse = read_rows(bse_data, struct.unpack_from("<H", bse_data, 2)[0])
    static = read_rows(scus, STATIC_TABLE_FILE, STATIC_ROWS)
    monster_snd = open(prot_path("0891"), "rb").read()
    banks = {}
    for bank_id in sorted(set(CATEGORY_BANKS.values())):
        data = open(prot_path(bank_id), "rb").read()
        banks[bank_id] = Vab(data, data.find(b"pBAV"))

    common_dir = os.path.join(args.out, "Battle", "Common")
    os.makedirs(common_dir, exist_ok=True)
    for name in args.names:
        folder = os.path.join(args.out, "Party", name)
        os.makedirs(folder, exist_ok=True)
        manifest = {"name": name, "folder": "Party/%s" % name, "clips": [], "sounds": [], "notifies": [],
                    "skipped": []}
        written = set()

        def own(fname, write):
            if fname not in written:
                info = write(os.path.join(folder, fname + ".wav"))
                manifest["sounds"].append(dict(sound=fname, **info))
                written.add(fname)

        def common(fname, row, bank_id):
            if fname not in written:
                pcm = banks[bank_id].render(row["program"], row["tone"], row["note"], row["voices"])
                write_wav(os.path.join(common_dir, fname + ".wav"), pcm)
                written.add(fname)

        for c in clips_of(name, prot_path(PLAYER_FILES[name]), args.tools, scus):
            glb_asset = "AS_%s_Battle_%s" % (name, clip_suffix(c["glb"]))
            clip = "AS_%s_Battle_%s" % (name, c["clip"])
            seconds_per_frame = 1.0 / (7.5 * c["rate"]) if c["rate"] else 1.0 / 15
            # Chain Cut: com outra ação na fila, o jogo corta o clip quando o quadro passa do 1º beat
            # + 2 (trava +0x76 em 0; FUN_80047430). Os golpes de direção: beat 7, corte no quadro 10.
            chain_cut = round((c["beat"] + 3) * seconds_per_frame, 4) if not c["lock"] else None
            manifest["clips"].append({"glb": glb_asset, "clip": clip, "id": "0x%02X" % c["id"],
                                      "rate_scale": c["rate_scale"], "chain_cut": chain_cut})
            notes = []

            is_victory = SPIRIT < c["id"] < ART_STARTER
            cues = [p for i, p in enumerate(c["cues"]) if i == 0 or p != c["cues"][i - 1]]
            for frame, cue in cues:
                if 0xC8 <= cue <= 0xFF and cue != 0xFA:
                    base = "SW_%s_Voice%02X" % (name, cue)
                    takes = [(base + "_A", False), (base + "_B", True)] if cue in SHOUTS else [(base, False)]
                    for fname, take_b in takes:
                        def write_xa(path, cue=cue, take_b=take_b):
                            xa, ch, pcm, nch, rate = xa_voice(args.xa, cue, take_b)
                            write_wav_raw(path, pcm, nch, rate)
                            return {"xa": xa, "channel": ch, "seconds": round(len(pcm) / nch / rate, 3)}
                        own(fname, write_xa)
                    notes.append((frame, takes[0][0], manifest["folder"], "0x%02X" % cue))
                elif cue < 0x64:
                    sound = "SW_Battle_Cue%02X" % cue
                    common(sound, static[cue - 1 if cue < 0x48 else cue], CATEGORY_BANKS[static[cue - 1 if cue < 0x48 else cue]["category"]])
                    notes.append((frame, sound, "Battle/Common", "0x%02X" % cue))
                elif cue >= 0xA7:
                    sound = "SW_Battle_Cue%02X" % cue
                    common(sound, bse[cue - 100], CATEGORY_BANKS[2])
                    notes.append((frame, sound, "Battle/Common", "0x%02X" % cue))
                elif is_victory:
                    # Na pose de vitória o slot 7 é a voz que o sequenciador acabou de carregar
                    row = bse[cue - 100]
                    snd_clip = victory_clip(scus, name, c["id"])
                    sound = "SW_%s_VictoryVoice%02X" % (name, snd_clip)

                    def write_victory(path, snd_clip=snd_clip, row=row):
                        pcm = victory_bank(monster_snd, snd_clip).render(row["program"], row["tone"], row["note"],
                                                                          row["voices"])
                        write_wav(path, pcm)
                        return {"monster_snd_clip": "0x%02X" % snd_clip, "seconds": round(len(pcm) / 44100, 3)}
                    own(sound, write_victory)
                    notes.append((frame, sound, manifest["folder"], "0x%02X" % cue))
                else:
                    manifest["skipped"].append({"clip": clip, "frame": frame, "cue": "0x%02X" % cue,
                                                "why": "banco do monstro da luta (VAB slot 7)"})
            for frame, sound, where, cue in notes:
                manifest["notifies"].append({"clip": clip, "frame": frame, "time": round(frame * seconds_per_frame, 3),
                                             "sound": sound, "folder": where, "cue": cue})

        manifest["notifies"].sort(key=lambda n: (n["clip"], n["frame"]))
        with open(os.path.join(folder, "sounds.json"), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
        print("%s: %d clips, %d notifies, vozes %s" % (
            name, len(manifest["clips"]), len(manifest["notifies"]),
            ", ".join(s["sound"][len(name) + 4:] for s in manifest["sounds"]) or "-"))
        for c in manifest["clips"]:
            print("   %-38s -> %-36s %s  x%g" % (c["glb"], c["clip"], c["id"], c["rate_scale"]))
        for s in manifest["skipped"]:
            print("   [fora] %-34s quadro %2d  %s — %s" % (s["clip"], s["frame"], s["cue"], s["why"]))


if __name__ == "__main__":
    main()
