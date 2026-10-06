"""
Gera o mapa de status do README (Docs/status/map.en.svg e map.pt-BR.svg).

    python Extras/generate_status_map.py

Painel da esquerda: uma célula por UFUNCTION lida dos headers em Source/JRPGFramework/Public,
mais a API planejada em Docs/BATTLE_ROADMAP.md (que ainda não existe no código).
Painel da direita: os sistemas do plugin, com largura proporcional ao esforço estimado, e as
9 fases do roadmap de batalha.

Ao concluir um sistema ou uma fase, atualize SYSTEMS / ROADMAP_PHASES / API_STATE abaixo e
rode de novo. Só biblioteca padrão.
"""

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PUBLIC = ROOT / "Source" / "JRPGFramework" / "Public"
OUT = ROOT / "Docs" / "status"

OK, PARTIAL, PENDING = "ok", "partial", "pending"
SCORE = {OK: 1.0, PARTIAL: 0.5, PENDING: 0.0}

# ---------------------------------------------------------------------------------------------
# Painel 1 — API Blueprint
# ---------------------------------------------------------------------------------------------

# Ordem dos grupos e de que pasta(s) de Public/ vem cada um. Duas colunas.
API_GROUPS = [
    # (chave, rótulo en, rótulo pt, pastas, coluna)
    ("core", "Core + progression", "Core + progressão", ["Core"], 0),
    ("party", "Party", "Party", ["Party"], 0),
    ("camera", "Camera", "Câmera", ["Camera"], 0),
    ("webui", "WebUI (shell + JS bridge)", "WebUI (shell + ponte JS)", ["UI"], 0),
    ("inventory", "Inventory", "Inventário", ["Inventory"], 1),
    ("status", "Status", "Status", ["Status"], 1),
    ("audio", "Audio", "Áudio", ["Audio"], 1),
    ("world", "World state + mist", "Estado do mundo + névoa", ["World"], 1),
    ("shop", "Shop", "Loja", ["Shop"], 1),
    ("save", "Save / Load", "Save / Load", ["Save"], 1),
    ("battle", "Battle", "Batalha", ["Battle"], 1),
    ("arts", "Arts", "Arts", ["Arts"], 1),
]

# Funções que existem mas não fazem (tudo) o que prometem.
API_STATE = {
    "ApplyBattleItemEffect": (PENDING, "stub — only logs TODO", "stub — só registra TODO"),
    "TeachArtToCharacter": (PENDING, "stub — only logs TODO", "stub — só registra TODO"),
    "OpenDialogue": (PARTIAL, "final signature; the JS side only logs", "assinatura final; o JS só registra"),
}

# API nomeada em Docs/BATTLE_ROADMAP.md que ainda não existe.
API_PLANNED = {
    "battle": [
        ("BeginBattle", "Phase 5"), ("EndBattle", "Phase 5"), ("SubmitCommand", "Phase 5"),
        ("GetCombatants", "Phase 5"), ("GetActiveSlot", "Phase 5"),
        ("InitFromEnemyData", "Phase 3 · AJRPGRoamingEnemy"), ("SeatFor", "Phase 4 · ABattleStage"),
        ("OnBattleCommand", "Phase 7 · WebUI bridge"), ("OnBattleArtsButton", "Phase 7 · WebUI bridge"),
        ("OnBattleArtsCommit", "Phase 7 · WebUI bridge"), ("OnBattleTargetChanged", "Phase 7 · WebUI bridge"),
    ],
    "arts": [
        ("DirectionByteFor", "Phase 6"), ("RecognizeArtSequence", "Phase 6"),
        ("GetKnownArts", "Phase 6"), ("CommitArtsChain", "Phase 6"),
    ],
}

# ---------------------------------------------------------------------------------------------
# Painel 2 — sistemas (peso = esforço estimado, só para a proporção visual e a porcentagem)
# ---------------------------------------------------------------------------------------------

def S(label, state, weight, en, pt, label_pt=None):
    return {"label": label, "label_pt": label_pt or label, "state": state, "w": weight, "en": en, "pt": pt}

SYSTEMS = [
    [  # linha 1
        S("Core", OK, 3, "Gold, playtime, current map, New Game, DataTable resolution",
          "Gold, tempo de jogo, mapa atual, New Game, resolução de DataTables"),
        S("Save", OK, 2.5, "15 slots, automatic gather/restore, map change + teleport, versioning",
          "15 slots, coleta/restauração automática, troca de mapa + teleporte, versionamento"),
        S("Inventory", OK, 2, "DT_Items (225 rows), stacks to 99, item use routed to other subsystems",
          "DT_Items (225 linhas), pilhas até 99, uso de item roteado para outros subsistemas", "Inventário"),
        S("World", OK, 1.5, "Event flags, chests, Revival Trees, bIsInField",
          "Flags de evento, baús, Revival Trees, bIsInField", "Mundo"),
        S("Shop", OK, 2, "DT_Shops (32), atomic buy/sell, platinum_card, flag gating",
          "DT_Shops (32), compra/venda atômica, platinum_card, liberação por flag", "Loja"),
        S("Audio", OK, 1.5, "BGM persisting across maps with crossfade, 4 volume channels",
          "BGM persistente entre mapas com crossfade, 4 canais de volume", "Áudio"),
    ],
    [  # linha 2
        S("Party", OK, 3, "Roster vs formation, XP split among survivors, level-up — the UI does not follow the formation yet",
          "Roster vs formação, XP dividido entre sobreviventes, level-up — a UI ainda não segue a formação"),
        S("Status", OK, 2.5, "5 equip slots, conditions, Ra-Seru 1..9, elemental affinity, effective stat",
          "5 slots de equipamento, condições, Ra-Seru 1..9, afinidade elemental, stat efetivo"),
        S("Progression", OK, 2, "XP curve, stat growth from the disc, Normal / Hard / Juggernaut",
          "Curva de XP, crescimento de stats tirado do disco, Normal / Hard / Juggernaut", "Progressão"),
        S("Camera", OK, 3, "13 presets, 8 shakes, level cameras + zones, conversation, FrameGroup / Orbit",
          "13 presets, 8 tremores, câmeras de nível + zonas, conversa, FrameGroup / Orbit", "Câmera"),
        S("Data", OK, 1.5, "8 DataTables as CSV, the source of truth", "8 DataTables em CSV, a fonte da verdade", "Dados"),
        S("Mist", PARTIAL, 2, "Living volumetric mist: C++ and shader done, material assets still to be built in the editor",
          "Névoa volumétrica viva: C++ e shader prontos, falta montar os assets do material no editor", "Névoa"),
    ],
    [  # linha 3
        S("WebUI", OK, 4, "HTML SPA shell: menu, items, party, shop, save/load, options, main menu, dev screen; gamepad + keyboard + mouse, responsive scaling",
          "Shell SPA em HTML: menu, itens, party, loja, save/load, opções, menu principal, tela de dev; gamepad + teclado + mouse, escala responsiva"),
        S("Ultralight", OK, 3, "Dedicated render thread, custom D3D11 GPU driver, shared texture",
          "Thread de render dedicada, driver de GPU D3D11 próprio, textura compartilhada"),
        S("Studio", OK, 2, "LegaiaStudio: local web panel that edits the CSVs and runs the builds",
          "LegaiaStudio: painel web local que edita os CSVs e roda os builds"),
        S("Equip UI", PENDING, 1.5, "Equip screen — Status works, there is no UI for it",
          "Tela de equipamento — o Status funciona, falta a UI", "Equipar"),
        S("Options", PARTIAL, 1.5, "Screen built; values not wired to UGameUserSettings / AudioSubsystem",
          "Tela pronta; valores não ligados a UGameUserSettings / AudioSubsystem", "Opções"),
    ],
    [  # linha 4
        S("Battle", PENDING, 10, "Stub — 71 lines that log TODO. 9-phase plan in Docs/BATTLE_ROADMAP.md",
          "Stub — 71 linhas que registram TODO. Plano de 9 fases em Docs/BATTLE_ROADMAP.md", "Batalha"),
        S("Arts", PENDING, 4, "Stub — directional Arts and the new AP (roadmap phase 6)",
          "Stub — Arts direcionais e o novo AP (fase 6 do roadmap)"),
        S("Dialogue", PARTIAL, 4, "Conversation camera and OpenDialogue signature done; no dialogue runtime yet",
          "Câmera de conversa e assinatura de OpenDialogue prontas; falta o runtime de diálogo", "Diálogo"),
        S("Cutscene", PENDING, 3, "Nothing written; camera pieces already exist",
          "Nada escrito; as peças de câmera já existem"),
        S("L10n", PARTIAL, 3, "FText everywhere + Language option; no culture switching or string pipeline",
          "FText em tudo + opção de idioma; sem troca de cultura nem pipeline de strings"),
    ],
]

ROADMAP_PHASES = [
    (PENDING, "Extract assets from the disc", "Extrair assets do disco"),
    (PENDING, "Battle data (TOML → CSV → DataTable)", "Dados de batalha (TOML → CSV → DataTable)"),
    (PENDING, "PS1 3D assets (GLB + skin → SkeletalMesh)", "Assets 3D do PS1 (GLB + skin → SkeletalMesh)"),
    (PENDING, "Enemies roaming the map", "Inimigos andando pelo mapa"),
    (PENDING, "Battle stage", "Palco da batalha"),
    (PENDING, "Basic combat", "Combate básico"),
    (PENDING, "Directional Arts + new AP", "Arts direcionais + novo AP"),
    (PENDING, "Presentation (HUD, camera, sound)", "Apresentação (HUD, câmera, som)"),
    (PENDING, "Magic and Seru", "Magia e Seru"),
]

# ---------------------------------------------------------------------------------------------

TEXT = {
    "en": {
        "title": "Status",
        "b_systems": "systems*", "b_battle": "battle roadmap", "b_engine": "engine",
        "api": "Blueprint API*: {ok} shipped · {stub} stubs · {plan} planned",
        "sys": "Plugin systems*: {pct}% (weighted by effort)",
        "road": "Battle roadmap: {done}/{n} phases",
        "phase": "Phase",
        "planned": "planned", "stub": "stub",
        "legend": ("Done", "Partial", "Not started / stub"),
        "foot": "* One cell per UFUNCTION in Source/JRPGFramework/Public, plus the API named in Docs/BATTLE_ROADMAP.md. Systems: weighted by estimated effort.",
        "asof": "dev @ {rev} · {date}",
        "aria": "Status: systems {pct}%, battle roadmap {done}/{n}",
    },
    "pt-BR": {
        "title": "Status",
        "b_systems": "sistemas*", "b_battle": "roadmap de batalha", "b_engine": "engine",
        "api": "API Blueprint*: {ok} prontas · {stub} stubs · {plan} planejadas",
        "sys": "Sistemas do plugin*: {pct}% (ponderado por esforço)",
        "road": "Roadmap de batalha: {done}/{n} fases",
        "phase": "Fase",
        "planned": "planejada", "stub": "stub",
        "legend": ("Pronto", "Parcial", "Não iniciado / stub"),
        "foot": "* Uma célula por UFUNCTION em Source/JRPGFramework/Public, mais a API citada em Docs/BATTLE_ROADMAP.md. Sistemas: ponderados por esforço estimado.",
        "asof": "dev @ {rev} · {date}",
        "aria": "Status: sistemas {pct}%, roadmap de batalha {done}/{n}",
    },
}

CSS = """<style>
  .bg { fill: #ffffff; }
  .rule { stroke: #d0d7de; }
  .t1 { fill: #1f2328; }
  .t2 { fill: #59636e; }
  .ok { fill: #0ca30c; }
  .partial { fill: #fab219; }
  .pending { fill: #c4c8ce; }
  .hatch { stroke: #8a5a00; }
  .badge-k { fill: #59636e; }
  .badge-t { fill: #ffffff; }
  .txt-dark { fill: #1f2328; }
  .txt-pending { fill: #1f2328; }
  .panel { fill: #ffffff; }
  @media (prefers-color-scheme: dark) {
    .bg { fill: #0d1117; }
    .rule { stroke: #30363d; }
    .t1 { fill: #e6edf3; }
    .t2 { fill: #9198a1; }
    .pending { fill: #4b525c; }
    .badge-k { fill: #3d444d; }
    .txt-pending { fill: #e6edf3; }
    .panel { fill: #0d1117; }
  }
</style>"""


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def text_w(s, size):
    """Largura aproximada de texto sans-serif."""
    return len(s) * size * 0.58


def parse_ufunctions(folder):
    names = []
    for header in sorted((PUBLIC / folder).glob("*.h")):
        src = header.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"UFUNCTION\s*\(", src):
            # pula os parênteses balanceados do UFUNCTION(...)
            i, depth = m.end(), 1
            while depth and i < len(src):
                depth += {"(": 1, ")": -1}.get(src[i], 0)
                i += 1
            decl = src[i:src.index("(", i)]
            names.append((re.findall(r"[A-Za-z_]\w*", decl)[-1], header.name))
    return names


def git_rev():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception:
        return "?"


def git_date():
    try:
        return subprocess.check_output(["git", "log", "-1", "--format=%cs"], cwd=ROOT, text=True).strip()
    except Exception:
        return ""


def badge(out, x, key, value, cls, value_cls="badge-t"):
    kw = text_w(key, 12) + 14
    vw = text_w(value, 12) + 14
    out.append(f'<rect class="badge-k" x="{x}" y="56" width="{kw + 3:.1f}" height="22" rx="3"/>')
    out.append(f'<rect class="{cls}" x="{x + kw:.1f}" y="56" width="{vw:.1f}" height="22" rx="3"/>')
    out.append(f'<rect class="{cls}" x="{x + kw:.1f}" y="56" width="4" height="22"/>')
    out.append(f'<text class="badge-t" x="{x + kw / 2:.1f}" y="71" font-size="12" text-anchor="middle">{esc(key)}</text>')
    out.append(f'<text class="{value_cls}" x="{x + kw + vw / 2:.1f}" y="71" font-size="12" font-weight="600" text-anchor="middle">{esc(value)}</text>')
    return x + kw + vw + 12


def cell_text_cls(state):
    return {OK: "badge-t", PARTIAL: "txt-dark", PENDING: "txt-pending"}[state]


def hatch(out, x, y, w, h, rx=0):
    r = f' rx="{rx}"' if rx else ""
    out.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}"{r} fill="url(#hatch)" pointer-events="none"/>')


def build(lang):
    T = TEXT[lang]
    pt = lang == "pt-BR"
    W, H = 900, 446

    # --- dados do painel 1
    groups = []
    for key, en, ptl, folders, col in API_GROUPS:
        cells = []
        for folder in folders:
            for name, header in parse_ufunctions(folder):
                state, note_en, note_pt = API_STATE.get(name, (OK, "", ""))
                note = note_pt if pt else note_en
                cells.append((state, f"{name} · {header}" + (f" — {note}" if note else "")))
        for name, where in API_PLANNED.get(key, []):
            where_l = where.replace("Phase", "Fase") if pt else where
            cells.append((PENDING, f"{name} — {T['planned']} ({where_l})"))
        groups.append((ptl if pt else en, col, cells))

    n_ok = sum(1 for _, _, c in groups for s, _ in c if s == OK or s == PARTIAL)
    n_stub = sum(1 for _, _, c in groups for s, t in c if s == PENDING and "stub" in t)
    n_plan = sum(1 for _, _, c in groups for s, t in c if s == PENDING and "stub" not in t)

    # --- porcentagem dos sistemas
    tot = sum(s["w"] for row in SYSTEMS for s in row)
    got = sum(s["w"] * SCORE[s["state"]] for row in SYSTEMS for s in row)
    pct = f"{100 * got / tot:.1f}"
    done = sum(1 for p in ROADMAP_PHASES if p[0] == OK)
    n_ph = len(ROADMAP_PHASES)

    out = []
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
               f'font-family="-apple-system,BlinkMacSystemFont,\'Segoe UI\',Helvetica,Arial,sans-serif" role="img" aria-labelledby="status-title">')
    out.append(f'<title id="status-title">{esc(T["aria"].format(pct=pct, done=done, n=n_ph))}</title>')
    out.append(CSS)
    out.append('<defs><pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
               '<line class="hatch" x1="0" y1="0" x2="0" y2="6" stroke-width="1.5" stroke-opacity=".55"/></pattern></defs>')
    out.append(f'<rect class="bg" width="{W}" height="{H}" rx="6"/>')
    out.append(f'<text class="t1" x="16" y="34" font-size="24" font-weight="600">{T["title"]}</text>')
    out.append(f'<text class="t2" x="884" y="34" font-size="12" text-anchor="end">'
               f'Legend of Legaia Remake · JRPGFramework · {esc(T["asof"].format(rev=git_rev(), date=git_date()))}</text>')
    out.append('<line class="rule" x1="16" y1="44" x2="884" y2="44" stroke-width="1"/>')

    p = float(pct)
    x = badge(out, 16, T["b_systems"], f"{pct}%", OK if p >= 80 else PARTIAL, "badge-t" if p >= 80 else "txt-dark")
    x = badge(out, x, T["b_battle"], f"{done}/{n_ph}", OK if done == n_ph else PENDING,
              "badge-t" if done == n_ph else "txt-pending")
    badge(out, x, T["b_engine"], "UE 5.8.1", OK)

    # --- painel 1: grade de UFUNCTIONs em duas colunas
    PX, PY, PW, PH = 16, 112, 424, 270
    out.append(f'<text class="t1" x="{PX}" y="102" font-size="14">{esc(T["api"].format(ok=n_ok, stub=n_stub, plan=n_plan))}</text>')
    out.append(f'<rect class="panel" x="{PX}" y="{PY}" width="{PW}" height="{PH}"/>')
    COLS, PITCH, CELL = 20, 10.3, 9.4
    col_x = [PX + 1.5, PX + 1.5 + COLS * PITCH + 6]
    col_y = [PY + 2, PY + 2]
    for label, col, cells in groups:
        y = col_y[col]
        x0 = col_x[col]
        out.append(f'<text class="t2" x="{x0:.1f}" y="{y + 9:.1f}" font-size="10">{esc(label)} '
                   f'<tspan class="t2" fill-opacity=".8">({len(cells)})</tspan></text>')
        y += 12
        for i, (state, tip) in enumerate(cells):
            cx = x0 + (i % COLS) * PITCH
            cy = y + (i // COLS) * PITCH
            out.append(f'<rect class="{state}" x="{cx:.1f}" y="{cy:.1f}" width="{CELL}" height="{CELL}"><title>{esc(tip)}</title></rect>')
            if state == PARTIAL:
                hatch(out, cx, cy, CELL, CELL)
        rows = (len(cells) + COLS - 1) // COLS
        col_y[col] = y + rows * PITCH + 5
    assert max(col_y) <= PY + PH, f"painel 1 estourou: {max(col_y)}"

    # --- painel 2: sistemas
    RX, RW = 462, 422
    out.append(f'<text class="t1" x="{RX}" y="102" font-size="14">{esc(T["sys"].format(pct=pct))}</text>')
    row_h = [56, 56, 50, 58]
    y = PY + 1.5
    for row, h in zip(SYSTEMS, row_h):
        rw = sum(s["w"] for s in row)
        x = RX
        for s in row:
            w = RW * s["w"] / rw
            label = s["label_pt"] if pt else s["label"]
            tip = f"{label}: {s['pt'] if pt else s['en']}"
            out.append(f'<rect class="{s["state"]}" x="{x:.1f}" y="{y:.1f}" width="{w - 1:.1f}" height="{h - 1:.1f}"><title>{esc(tip)}</title></rect>')
            if s["state"] == PARTIAL:
                hatch(out, x, y, w - 1, h - 1)
            fs = 11
            while fs > 8 and text_w(label, fs) > w - 6:
                fs -= 0.5
            out.append(f'<text class="{cell_text_cls(s["state"])}" x="{x + (w - 1) / 2:.1f}" y="{y + (h - 1) / 2 + fs * 0.36:.1f}" '
                       f'font-size="{fs:g}" text-anchor="middle" pointer-events="none">{esc(label)}</text>')
            x += w
        y += h

    # --- roadmap de batalha
    y += 8
    out.append(f'<text class="t2" x="{RX}" y="{y + 10:.1f}" font-size="11">{esc(T["road"].format(done=done, n=n_ph))}</text>')
    y += 16
    bh = PY + PH - y
    bw = RW / n_ph
    for i, (state, en, ptl) in enumerate(ROADMAP_PHASES):
        x = RX + i * bw
        out.append(f'<rect class="{state}" x="{x:.1f}" y="{y:.1f}" width="{bw - 1:.1f}" height="{bh:.1f}">'
                   f'<title>{esc(T["phase"])} {i} — {esc(ptl if pt else en)}</title></rect>')
        if state == PARTIAL:
            hatch(out, x, y, bw - 1, bh)
        out.append(f'<text class="{cell_text_cls(state)}" x="{x + (bw - 1) / 2:.1f}" y="{y + bh / 2 + 4:.1f}" '
                   f'font-size="11" text-anchor="middle" pointer-events="none">{i}</text>')

    # --- legenda
    ly = PY + PH + 14
    x = 16
    for cls, label in zip((OK, PARTIAL, PENDING), T["legend"]):
        out.append(f'<rect class="{cls}" x="{x}" y="{ly}" width="12" height="12" rx="2"/>')
        if cls == PARTIAL:
            hatch(out, x, ly, 12, 12, 2)
        out.append(f'<text class="t2" x="{x + 17}" y="{ly + 10}" font-size="12">{esc(label)}</text>')
        x += 17 + text_w(label, 12) + 22
    out.append(f'<text class="t2" x="16" y="{ly + 32}" font-size="10.5">{esc(T["foot"])}</text>')
    out.append(f'<text class="t2" x="16" y="{ly + 46}" font-size="10.5">'
               f'{"Gerado por" if pt else "Generated by"} Extras/generate_status_map.py · '
               f'{"passe o mouse nas células para detalhes" if pt else "hover the cells for details"}</text>')
    out.append("</svg>")
    return "\n".join(out) + "\n", pct


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for lang in ("en", "pt-BR"):
        svg, pct = build(lang)
        path = OUT / f"map.{lang}.svg"
        path.write_text(svg, encoding="utf-8", newline="\n")
        print(f"{path.relative_to(ROOT)}  ({pct}%)")


if __name__ == "__main__":
    main()
