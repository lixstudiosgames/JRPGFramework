#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
arts_rules.py — o reconhecimento das Arts e o validador do DT_Arts, em Python.

É o espelho do C++ (Source/JRPGFramework/Private/Arts/JRPGArtsRules.cpp e JRPGArtsValidator.cpp)
para quem não tem o editor aberto: o generate_arts_csv.py valida o que gerou e o LegaiaStudio
mostra os problemas da aba Arts ao salvar. Só as regras que não precisam de asset (esqueleto do
Anim, notifies de golpe) — essas o C++ confere no Data Validation e no teste de automação.

    python Extras/arts_rules.py                       # valida Docs/Data/DT_Arts.csv
    python Extras/arts_rules.py Vahn High Low High    # o que os botões reconhecem para o herói

O tokenizer é o do original (FUN_801EED1C; RE crates/art/src/tokenize.rs): do fim da entrada para
o começo, cada Art casada troca o último botão pelo starter e mantém os anteriores, e a busca
recomeça logo depois — um botão pode ser de duas Arts. A Hyper cobre a faixa inteira.
Guia: Docs/Guides/ARTS.md.
"""

import csv
import os
import sys

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARTS_CSV = os.path.join(PLUGIN_DIR, "Docs", "Data", "DT_Arts.csv")

BUTTONS = ("Arms", "RaSeru", "High", "Low")
MAX_CHAIN_BUTTONS = 16  # o buffer do original (UJRPGArtsRules::MaxChainButtons)
BASIC_STEPS = {"SwingL", "SwingR", "SwingD", "SwingU", "ArtStarter", "SpecialStarter"}
TOKEN_KINDS = ("Regular", "Hyper")

# O "find" de cada Super do original, como Arts (RE docs/formats/art-data.md › Super Arts). É o que o
# Command dela tem que tokenizar — não a Sequence, que troca a última Art pelo finalizador.
DISC_SUPER_CHAINS = {
    "vahn_tri_somersault": ["vahn_somersault", "vahn_cyclone", "vahn_somersault"],
    "vahn_maximum_blow": ["vahn_charging_scorch", "vahn_slash_kick", "vahn_power_punch"],
    "vahn_fire_tackle": ["vahn_hyper_elbow", "vahn_power_punch", "vahn_charging_scorch"],
    "vahn_power_slash": ["vahn_charging_scorch", "vahn_somersault", "vahn_slash_kick"],
    "vahn_rolling_combo": ["vahn_spin_combo", "vahn_power_punch", "vahn_pk_combo"],
    "noa_triple_lizard": ["noa_bird_step", "noa_swan_driver", "noa_lizard_tail"],
    "noa_super_javelin": ["noa_rushing_gale", "noa_sonic_javelin"],
    "noa_super_tempest": ["noa_dolphin_attack", "noa_tempest_break"],
    "noa_love_you": ["noa_mirage_lancer", "noa_lizard_tail", "noa_tough_love"],
    "noa_dragon_fangs": ["noa_lizard_tail", "noa_swan_driver", "noa_acrobatic_blitz"],
    "gala_back_punch_x3": ["gala_ironhead", "gala_flying_knee_attack", "gala_back_punch"],
    "gala_super_ironhead": ["gala_flying_knee_attack", "gala_head_splitter", "gala_ironhead"],
    "gala_rushing_crush": ["gala_battering_ram", "gala_flying_knee_attack", "gala_head_splitter"],
    "gala_heavens_drop": ["gala_flying_knee_attack", "gala_head_splitter", "gala_black_rain"],
    "gala_neo_static_raising": ["gala_back_punch", "gala_guillotine", "gala_neo_raising"],
}


def parse_array(text):
    """'(a,b,"c")' -> ['a', 'b', 'c']."""
    text = (text or "").strip()
    if text.startswith("(") and text.endswith(")"):
        text = text[1:-1]
    return [p.strip().strip('"') for p in text.split(",") if p.strip()]


def _int(text):
    try:
        return int(str(text).strip() or 0)
    except ValueError:
        return 0


class Art:
    def __init__(self, key, rec, index):
        self.key = key
        self.index = index
        self.key_column = rec.get("Key", key)
        self.character = rec.get("Character", "")
        self.kind = rec.get("Kind", "Regular") or "Regular"
        self.command = parse_array(rec.get("Command", ""))
        self.ap_gain = _int(rec.get("APGain"))
        self.ap_cost = _int(rec.get("APCost"))
        self.anim = (rec.get("Anim") or "").strip()
        self.hit_power = parse_array(rec.get("HitPower", ""))
        self.hit_target = parse_array(rec.get("HitTarget", ""))
        self.sequence = parse_array(rec.get("Sequence", ""))
        self.action_constant = _int(rec.get("ActionConstant"))
        self.unlock = (rec.get("Unlock") or "ByKind").strip() or "ByKind"
        self.speed_scale = _int(rec.get("SpeedScale"))

    def effective_unlock(self):
        if self.unlock != "ByKind":
            return self.unlock
        return {"Regular": "OnUse", "Hyper": "Taught", "Super": "ChainKnown", "Miracle": "RaSeru"}.get(self.kind, "ByKind")


def load_csv(path=ARTS_CSV):
    """{row name: Art}, na ordem do arquivo."""
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    return from_table(rows[0][1:] if rows else [], rows[1:])


def from_table(columns, rows):
    """Do formato do LegaiaStudio: columns sem o RowName, rows com o RowName na posição 0."""
    arts = {}
    for i, r in enumerate(rows):
        if not r or not r[0].strip():
            continue
        rec = dict(zip(columns, r[1:]))
        arts[r[0].strip()] = Art(r[0].strip(), rec, i)
    return arts


def from_dicts(rows):
    """De uma lista de dicts com "---" (o formato do legaia_toml.write_rows)."""
    return {r["---"]: Art(r["---"], {k: str(v) for k, v in r.items()}, i) for i, r in enumerate(rows)}


def token_arts(arts, hero):
    """As Arts que o tokenizer procura, na ordem da grade do original (ver TokenArts no C++)."""
    cat = [a for a in arts.values() if a.character == hero and a.kind in TOKEN_KINDS and a.command]
    cat.sort(key=lambda a: (a.action_constant <= 0, a.action_constant if a.action_constant > 0 else 0,
                            -len(a.command), a.index))
    return cat


def tokenize(catalog, buttons, can_match=None, excluded=()):
    """[(key, primeiro botão, último botão)], pela ordem em que terminam."""
    q = [["B", b, i] for i, b in enumerate(buttons)]
    out = []
    for start in range(len(buttons) - 1, -1, -1):
        for art in catalog:
            cmd = art.command
            pos, matched = start, 0
            while pos < len(q):
                e = q[pos]
                if e[0] == "B" and e[1] == cmd[matched]:
                    matched += 1
                    if matched == len(cmd):
                        first_q = pos - len(cmd) + 1
                        first, last = q[first_q][2], e[2]
                        if (art.key, last) in excluded or (can_match and not can_match(art)):
                            pos, matched = first_q + 1, 0
                            continue
                        out.append((art.key, first, last))
                        if art.kind == "Hyper":
                            for j in range(first_q, pos):
                                q[j] = ["X", None, -1]
                        q[pos] = ["S", None, -1]
                        q.insert(pos + 1, ["A", None, -1])
                        pos, matched = start, 0
                else:
                    pos -= matched
                    matched = 0
                pos += 1
    out.sort(key=lambda t: t[2])
    return out


def own_arts(arts, key):
    """As Arts de uma Super / Miracle: o que o Command dela tokeniza."""
    a = arts[key]
    return [t[0] for t in tokenize(token_arts(arts, a.character), a.command)]


def validate(arts):
    """[(severidade 'error' | 'warn', row, mensagem)] — as regras do UJRPGArtsValidator sem os assets."""
    issues = []

    def err(key, msg):
        issues.append(("error", key, msg))

    def warn(key, msg):
        issues.append(("warn", key, msg))

    for key, a in arts.items():
        part = a.kind == "Part"
        special = a.kind in ("Hyper", "Super", "Miracle")
        if a.key_column and a.key_column != key:
            warn(key, "a coluna Key (%s) não é o row name" % a.key_column)
        if not a.character:
            err(key, "sem Character")
        if a.kind not in ("Regular", "Hyper", "Super", "Miracle", "Part"):
            err(key, "Kind desconhecido: %s" % a.kind)
        bad = [b for b in a.command if b not in BUTTONS]
        if bad:
            err(key, "Command com botão desconhecido: %s" % ", ".join(bad))
        if part and a.command:
            err(key, "uma Part não se digita: o Command tem que ficar vazio")
        if len(a.command) > MAX_CHAIN_BUTTONS:
            err(key, "Command de %d botões: a cadeia leva no máximo %d" % (len(a.command), MAX_CHAIN_BUTTONS))
        if not part and not a.command:
            err(key, "sem Command: ninguém consegue digitar esta Art (só uma Part não tem)")
        if special and a.ap_cost <= 0:
            err(key, "um especial (Hyper / Super / Miracle) precisa de APCost")
        if a.kind == "Regular" and a.ap_cost > 0:
            err(key, "uma Regular soma AP (APGain), não gasta: APCost tem que ser 0")
        if len(a.hit_power) != len(a.hit_target):
            err(key, "HitPower tem %d golpe(s) e HitTarget %d" % (len(a.hit_power), len(a.hit_target)))
        if a.speed_scale < 0:
            err(key, "SpeedScale negativo (0 = a regra do original)")
        if a.unlock not in ("ByKind", "OnUse", "Taught", "ChainKnown", "RaSeru"):
            err(key, "Unlock desconhecido: %s" % a.unlock)
        if a.unlock == "ChainKnown" and a.kind in TOKEN_KINDS:
            warn(key, "Unlock = ChainKnown numa Art solta não tem cadeia: vale como Taught")
        if part and a.unlock != "ByKind":
            warn(key, "uma Part não é aprendida: o Unlock dela não faz nada")

        if not part and a.command:
            for other_key, o in arts.items():
                if other_key != key and o.character == a.character and o.kind != "Part" and o.command == a.command:
                    err(key, "mesmo Command (%s) que %s" % (", ".join(a.command), other_key))
            if a.kind in TOKEN_KINDS:
                toks = tokenize(token_arts(arts, a.character), a.command)
                if not any(t == (key, 0, len(a.command) - 1) for t in toks):
                    err(key, "nunca é reconhecida: %s dá %s" % (", ".join(a.command),
                                                                ", ".join(t[0] for t in toks) or "(nenhuma)"))

        for step in a.sequence:
            if step == key:
                if not a.anim:
                    err(key, "a Sequence cita a própria linha, mas ela não tem Anim")
                continue
            if step in BASIC_STEPS:
                continue
            o = arts.get(step)
            if not o:
                err(key, "passo da Sequence %s não existe (nem Art, nem clip básico)" % step)
                continue
            if o.character != a.character:
                err(key, "passo da Sequence %s é de %s" % (step, o.character))
            if _reaches(arts, step, key, {step}):
                err(key, "passo da Sequence %s volta para esta linha (laço)" % step)

        if a.kind == "Super" and a.command:
            own = own_arts(arts, key)
            if not own and a.effective_unlock() == "ChainKnown":
                warn(key, "o Command não tem Art nenhuma: como ChainKnown sairia sem o jogador saber nada "
                          "(dê Unlock = Taught)")
            if key in DISC_SUPER_CHAINS and own != DISC_SUPER_CHAINS[key]:
                err(key, "o Command tokeniza em %s; no original a cadeia é %s"
                    % (", ".join(own) or "(nenhuma)", ", ".join(DISC_SUPER_CHAINS[key])))
    return issues


def _reaches(arts, step, target, visited):
    o = arts.get(step)
    if not o:
        return False
    for inner in o.sequence:
        if inner == step:
            continue
        if inner == target:
            return True
        if inner not in visited:
            visited.add(inner)
            if _reaches(arts, inner, target, visited):
                return True
    return False


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    arts = load_csv()
    if len(sys.argv) > 2:
        hero = sys.argv[1]
        words = [w.replace("-", "") for w in sys.argv[2:]]
        names = {"arms": "Arms", "a": "Arms", "raseru": "RaSeru", "r": "RaSeru", "high": "High", "h": "High",
                 "low": "Low", "l": "Low"}
        buttons = [names.get(w.lower()) for w in words]
        if None in buttons:
            sys.exit("botões: Arms, Ra-Seru, High, Low")
        for key, first, last in tokenize(token_arts(arts, hero), buttons):
            print("  [%d-%d] %s" % (first + 1, last + 1, key))
        return
    issues = validate(arts)
    for level, key, msg in issues:
        print("[arts] %s %s: %s" % ("ERRO " if level == "error" else "aviso", key, msg))
    errors = sum(1 for i in issues if i[0] == "error")
    print("[arts] %d linha(s), %d erro(s), %d aviso(s)" % (len(arts), errors, len(issues) - errors))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
