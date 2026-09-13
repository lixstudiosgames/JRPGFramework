#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_webui.py — Gera o shell.html único da WebUI a partir dos fontes em src/.

Uso:
    python Extras/build_webui.py

Entrada (Content/UI/WebUI/src/):
    shell.template.html  — esqueleto com os marcadores @@CSS@@, @@SECTIONS@@, @@SCRIPTS@@
    css/*.css            — concatenados em ordem alfabética (prefixo numérico)
    sections/*.html      — trechos de markup de cada tela, mesma ordenação
    js/*.js              — concatenados num único <script>, mesma ordenação

Saída:
    Content/UI/WebUI/shell.html (UTF-8 sem BOM) — NUNCA editar direto.

Sem dependências externas (stdlib only). Sem minificação de propósito:
o output continua legível para debug no Ultralight (que não tem devtools).
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Content" / "UI" / "WebUI" / "src"
OUT = ROOT / "Content" / "UI" / "WebUI" / "shell.html"

BANNER = (
    "<!--\n"
    "    ARQUIVO GERADO — NÃO EDITAR DIRETAMENTE.\n"
    "    Edite os fontes em Content/UI/WebUI/src/ e rode: python Extras/build_webui.py\n"
    "-->\n"
)


def fail(msg):
    print(f"[build_webui] ERRO: {msg}")
    sys.exit(1)


def read_sorted(folder, pattern):
    files = sorted(folder.glob(pattern), key=lambda p: p.name)
    return [(p.name, p.read_text(encoding="utf-8")) for p in files]


def build_css(css_files):
    """Concatena os CSS. Linhas @import são içadas para o topo do bloco
    mesclado (regra CSS: @import deve vir antes de qualquer regra). Com as
    fontes locais não deve sobrar nenhum @import — apenas robustez."""
    imports = []
    blocks = []
    import_re = re.compile(r"^\s*@import\b.*$", re.MULTILINE)

    for name, content in css_files:
        found = import_re.findall(content)
        if found:
            print(f"[build_webui] AVISO: @import encontrado em {name} (içado para o topo).")
            imports.extend(found)
            content = import_re.sub("", content)
        blocks.append(f"/* ==== {name} ==== */\n{content.strip()}")

    parts = imports + blocks
    return "\n\n".join(parts)


def build_sections(section_files):
    return "\n\n".join(content.strip() for _, content in section_files)


def build_js(js_files):
    """Concatena os JS com ';' separador (defesa contra ASI entre arquivos)."""
    blocks = []
    for name, content in js_files:
        blocks.append(f"// ==== {name} ====\n{content.strip()}")
    return "\n;\n".join(blocks)


def validate_sources(css_files, section_files, js_files):
    if not css_files:
        fail(f"nenhum .css encontrado em {SRC / 'css'}")
    if not section_files:
        fail(f"nenhum .html encontrado em {SRC / 'sections'}")
    if not js_files:
        fail(f"nenhum .js encontrado em {SRC / 'js'}")

    # A ponte só pode ser resolvida via getBridge() (lazy). Uma const capturada
    # fica stale entre sessões PIE (o C++ recria window.ue.uebridge no documento
    # vivo) — além de quebrar a concatenação com declarações duplicadas.
    bridge_decl = re.compile(r"\b(?:const|let|var)\s+bridge\s*=")
    for name, content in js_files:
        # Dentro de função (indentado) é permitido: é resolução lazy por chamada.
        for m in bridge_decl.finditer(content):
            line_start = content.rfind("\n", 0, m.start()) + 1
            indent = content[line_start:m.start()]
            if indent.strip() == "" and indent == "":
                fail(
                    f"{name}: declaração de 'bridge' no escopo de módulo. "
                    "Use sempre getBridge() (00_core.js) — referência capturada fica stale entre PIE."
                )

    # </script> dentro de um JS terminaria o <script> inline do shell no parse do HTML
    for name, content in js_files:
        if "</script" in content.lower():
            fail(f"{name}: contém '</script>' — quebraria o <script> inline do shell.")


def main():
    if not SRC.is_dir():
        fail(f"pasta de fontes não encontrada: {SRC}")

    template_path = SRC / "shell.template.html"
    if not template_path.is_file():
        fail(f"template não encontrado: {template_path}")
    template = template_path.read_text(encoding="utf-8")

    for marker in ("@@CSS@@", "@@SECTIONS@@", "@@SCRIPTS@@"):
        if marker not in template:
            fail(f"marcador {marker} ausente no shell.template.html")

    css_files = read_sorted(SRC / "css", "*.css")
    section_files = read_sorted(SRC / "sections", "*.html")
    js_files = read_sorted(SRC / "js", "*.js")

    validate_sources(css_files, section_files, js_files)

    html = template
    html = html.replace("@@CSS@@", build_css(css_files))
    html = html.replace("@@SECTIONS@@", build_sections(section_files))
    html = html.replace("@@SCRIPTS@@", build_js(js_files))

    # Banner logo após o <!DOCTYPE ...>
    doctype_end = html.find(">") + 1
    html = html[:doctype_end] + "\n" + BANNER + html[doctype_end:].lstrip("\n")

    OUT.write_text(html, encoding="utf-8", newline="\n")

    print(f"[build_webui] OK: {OUT}")
    print(f"[build_webui]   css:      {', '.join(n for n, _ in css_files)}")
    print(f"[build_webui]   sections: {', '.join(n for n, _ in section_files)}")
    print(f"[build_webui]   js:       {', '.join(n for n, _ in js_files)}")
    print(f"[build_webui]   tamanho:  {OUT.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()
