"""
monster_block.py — ler o bloco decodificado de um monstro do arquivo PROT 0867 (o que o
`asset monster-archive <0867> --id N --dump-block out.bin` escreve) e tirar dele o que a luta
precisa: o registro (elemento, classe do alvo, magias) e as entradas de ação (poder, quadros de
golpe, custo de AGL, rate, seletor de status, tamanho do clip).

Formato (legend-of-legaia-re, docs/formats/monster-animation.md e docs/subsystems/battle-formulas.md):

  registro   +0x1D elemento, +0x1E classe do alvo (a altura: 0 todo golpe alcança, 2 voando, 3 baixo),
             +0x1F classe de tamanho (14..48 no disco; dá o alcance do golpe, FUN_8004E2F0),
             +0x21..+0x23 ids de magia, +0x4A (u8) quantidade de ações, +0x4C u32[] offsets das
             ações (relativos ao começo do bloco)
  ação       +0x00 tag (e o 1º byte de poder), +0x00..+0x03 bytes de poder (um por golpe),
             +0x10..+0x13 quadros de golpe (lista ascendente terminada em zero), +0x74 custo de AGL
             (0xFF = não é ataque), +0x76 gate do evento, +0x78 rate, +0x7A seletor de status,
             +0x8C [u8 partes][u8 quadros]…

Nada daqui entra no repositório além do que os geradores escrevem nos CSVs (ARCHITECTURE.md, regra 48):
o bloco decodificado vem do disco do usuário e fica numa pasta temporária.
"""

import os
import struct
import subprocess
import tempfile

ACTION_COUNT_AT = 0x4A
ACTION_OFFSETS_AT = 0x4C


def dump_block(tool, archive, monster_id, workdir):
    """Roda `asset monster-archive <archive> --id N --dump-block` e devolve os bytes do bloco."""
    out = os.path.join(workdir, "block_%d.bin" % monster_id)
    subprocess.run([tool, "monster-archive", archive, "--id", str(monster_id), "--dump-block", out],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(out, "rb") as f:
        return f.read()


def parse_block(block):
    """O registro e as ações de um bloco: {element, target_class, magic, actions: [...]}."""
    count = block[ACTION_COUNT_AT]
    offsets = struct.unpack_from("<%dI" % count, block, ACTION_OFFSETS_AT)
    actions = []
    for index, off in enumerate(offsets):
        entry = block[off:]
        if len(entry) < 0x8E:
            continue
        hits = []
        for b in entry[0x10:0x14]:
            if b == 0:
                break
            hits.append(b)
        actions.append({
            "index": index,
            "tag": entry[0x00],
            "power": list(entry[0x00:0x04]),
            "hit_frames": hits,
            "agl_cost": entry[0x74],
            "gate": entry[0x76],
            "rate": entry[0x78],
            "status": entry[0x7A],
            "frames": entry[0x8D],
            # +0x0C a velocidade de raiz (unidades por vsync ao longo da direção; negativa = recua) e +0x0E o passo do fim do
            # clip (o quanto o clip deixa o corpo de onde começou, ao longo da direção), os dois i16
            "root_speed": struct.unpack_from("<h", entry, 0x0C)[0],
            "end_step": struct.unpack_from("<h", entry, 0x0E)[0],
        })
    return {
        "element": block[0x1D],
        "target_class": block[0x1E],
        "size_class": block[0x1F],
        "magic": [b for b in block[0x21:0x24] if b > 1],
        "actions": actions,
    }


class BlockReader:
    """Dumpa e lê blocos com a pasta temporária cuidada: with BlockReader(tool, archive) as r."""

    def __init__(self, tool, archive):
        self.tool = tool
        self.archive = archive
        self._dir = None

    def __enter__(self):
        self._dir = tempfile.TemporaryDirectory(prefix="legaia_blocks_")
        return self

    def __exit__(self, *exc):
        self._dir.cleanup()

    def read(self, monster_id):
        return parse_block(dump_block(self.tool, self.archive, monster_id, self._dir.name))
