#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
LegaiaStudio — painel local para gerenciar os dados do JRPGFramework.

Uso:
    python Extras/LegaiaStudio/studio.py            # sobe e abre o navegador
    python Extras/LegaiaStudio/studio.py --port 8790
    python Extras/LegaiaStudio/studio.py --no-browser

O que dá para fazer: editar as DataTables por ficha (nada de planilha), criar e
apagar linhas, montar o estoque das lojas escolhendo itens da DT_Items, e
disparar os builds da WebUI e do plugin com o log ao vivo. O CSV é escrito
atrás dos panos, com backup.

Arquitetura:
  - stdlib only (http.server + csv + json + subprocess). Sem pip.
  - O HTML/CSS/JS vive em web/, fora deste .py.
  - Bind SÓ em 127.0.0.1: a ferramenta escreve no código-fonte do plugin e
    dispara builds. Exposta na rede seria execução remota na máquina de dev.

De onde sai o schema: as colunas dos CSVs são as UPROPERTY dos structs
FTableRowBase do plugin. Este script parseia os headers C++ para saber campos,
tipos e valores válidos dos enums — assim o editor nunca dessincroniza.
"""

import argparse
import csv
import io
import json
import os
import re
import subprocess
import sys
import threading
import time
import webbrowser
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# O console do Windows abre em cp1252 e acentos levantam UnicodeEncodeError,
# escondendo a causa real de uma falha (mesma proteção do build_plugin.py).
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

# --- Caminhos -----------------------------------------------------------

STUDIO_DIR = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(STUDIO_DIR, "web")
PLUGIN_DIR = os.path.dirname(os.path.dirname(STUDIO_DIR))   # .../JRPGFramework

SOURCE_DIR = os.path.join(PLUGIN_DIR, "Source", "JRPGFramework", "Public")
CSV_DIR = os.path.join(PLUGIN_DIR, "Docs", "Data")
BACKUP_DIR = os.path.join(CSV_DIR, "_backup")
GAMEDATA_DIR = os.path.join(CSV_DIR, "gamedata")
FONTS_DIR = os.path.join(PLUGIN_DIR, "Content", "UI", "WebUI", "fonts")
EXTRAS_DIR = os.path.join(PLUGIN_DIR, "Extras")

DEFAULT_UNREAL_PROJECT = r"D:\JOGOS\PROJETOS UNREAL\5.8\Legaia"

# Tabelas conhecidas: DT <-> struct C++. Tabela nova entra aqui DEPOIS de o
# struct existir no plugin (a Unreal precisa de onde importar).
#   title/subtitle: campos usados na lista da esquerda
#   primary:        campos que aparecem no topo da ficha
#   title/subtitle/badge: o que a lista da esquerda mostra
#   primary: campos que ficam sempre à vista na ficha. Todo o resto entra numa
#            seção recolhida — o dia a dia é mexer em nome, preço e descrição,
#            não nas 20 colunas que já vieram certas do jogo original.
TABLES = [
    {"id": "DT_Items", "struct": "FItemData", "label": "Items", "icon": "item",
     "title": "Name", "subtitle": "Category", "badge": "BuyPrice", "badgeSuffix": " G",
     "primary": ["Name", "BuyPrice", "Category", "EffectDescription"]},

    {"id": "DT_Shops", "struct": "FShopData", "label": "Shops", "icon": "shop",
     "title": "ShopName", "subtitle": "Town", "badge": None, "badgeSuffix": "",
     "primary": ["ShopName", "Town", "Merchant", "Inventory", "FeaturedItems"]},

    {"id": "DT_Characters", "struct": "FCharacterData", "label": "Characters", "icon": "party",
     "title": "DisplayName", "subtitle": "RaSeru", "badge": "RaSeruElement", "badgeSuffix": "",
     # Identidade, Ra-Seru e afinidade já vêm certos do original: o que se
     # mexe agora são os atributos iniciais.
     "primary": ["DisplayName", "BaseHP", "BaseMP", "BaseAGL", "BaseATK",
                 "BaseUDF", "BaseLDF", "BaseSPD", "BaseINT"]},

    {"id": "DT_LevelCurve", "struct": "FLevelCurveRow", "label": "Level Curve", "icon": "curve",
     "title": "Level", "subtitle": "XPTotal", "badge": "XPFromPrevious", "badgeSuffix": " xp",
     "primary": ["Level", "XPTotal", "XPFromPrevious"]},

    {"id": "DT_Difficulty", "struct": "FDifficultyScaling", "label": "Difficulty", "icon": "shield",
     "title": "DisplayName", "subtitle": "Description", "badge": None, "badgeSuffix": "",
     "primary": ["DisplayName", "Description", "EnemyHP", "EnemyATK", "EnemyDEF",
                 "XPReward", "GoldReward"]},

    {"id": "DT_StatGrowth", "struct": "FStatGrowthRow", "label": "Stat Growth", "icon": "growth",
     # RowName ja e "Vahn_HP": title=None deixa ele mesmo ser o titulo.
     "title": None, "subtitle": "Character", "badge": "MaxValue", "badgeSuffix": " max",
     "primary": ["Character", "Stat", "GrowthStart", "MaxValue", "Jitter", "CurveRow",
                 "MinDifficulty"]},

    {"id": "DT_GrowthCurve", "struct": "FGrowthCurveRow", "label": "Growth Curve", "icon": "curve",
     # Cada linha e um level de origem; as 3 colunas sao as 3 curvas do original.
     "title": "Level", "subtitle": None, "badge": None, "badgeSuffix": "",
     "primary": ["Level", "Row0", "Row1", "Row2"]},

    {"id": "DT_CameraPresets", "struct": "FCameraPresetRow", "label": "Camera", "icon": "camera",
     "title": None, "subtitle": "FOV", "badge": None, "badgeSuffix": "",
     "primary": ["Distance", "PitchDeg", "YawDeg", "FOV"]},
]

# Campos que referenciam o RowName de outra tabela. É o que transforma
# "digitar a key na mão" em "escolher o item de uma lista".
REFERENCES = {
    ("DT_Shops", "Inventory"): "DT_Items",
    ("DT_Shops", "FeaturedItems"): "DT_Items",
}

# TOMLs do LegaiaRE que ainda não viraram DataTable (mostrados como pendentes)
KNOWN_TOML_ONLY = ["arts", "magic", "enemies", "bosses", "casino", "fishing",
                   "music", "sol_tower"]

# Builds expostos na sidebar. Os generate_*.py NÃO entram de propósito:
# reescrevem os CSVs inteiros a partir dos TOMLs e apagariam a edição manual.
BUILDS = {
    "webui": {"label": "Build WebUI", "script": "build_webui.py",
              "note": "Regenera o shell.html a partir de src/. Segundos."},
    "plugin": {"label": "Build Plugin", "script": "build_plugin.py",
               "note": "RunUAT: compila e empacota o plugin. Vários minutos."},
}


def load_config():
    cfg = {"unreal_project": DEFAULT_UNREAL_PROJECT}
    path = os.path.join(STUDIO_DIR, "studio.config.json")
    if os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                cfg.update(json.load(f))
        except (OSError, ValueError) as e:
            print("[studio] AVISO: studio.config.json inválido (%s) — usando defaults." % e)
    return cfg


CONFIG = load_config()


# ========================================================================
# PARSER DO SCHEMA (headers C++)
# ========================================================================

UPROP_RE = re.compile(
    r"UPROPERTY\s*\([^)]*\)\s*\n\s*"
    r"(?P<type>[A-Za-z_][\w:]*(?:\s*<[^>]+>)?)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?:=[^;]+)?;",
    re.MULTILINE,
)
ENUM_RE = re.compile(r"enum\s+class\s+(?P<name>E\w+)\s*:\s*uint8\s*\{(?P<body>[^}]*)\}", re.DOTALL)
STRUCT_RE = re.compile(
    r"struct\s+\w*\s*(?P<name>F\w+)\s*:\s*public\s+FTableRowBase\s*\{(?P<body>.*?)\n\};", re.DOTALL)


def _iter_headers():
    for root, _dirs, files in os.walk(SOURCE_DIR):
        for fn in files:
            if fn.endswith(".h"):
                yield os.path.join(root, fn)


def _read(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except (OSError, UnicodeDecodeError):
        return ""


def parse_enums():
    enums = {}
    for path in _iter_headers():
        for m in ENUM_RE.finditer(_read(path)):
            values = []
            for line in m.group("body").split("\n"):
                line = line.split("//")[0].strip()
                if not line:
                    continue
                token = line.split("UMETA")[0].split("=")[0].strip().rstrip(",").strip()
                if token and re.fullmatch(r"[A-Za-z_]\w*", token):
                    values.append(token)
            if values:
                enums[m.group("name")] = values
    return enums


def _classify(cpp_type, enums):
    t = cpp_type.replace(" ", "")
    if t.startswith("TArray<"):
        return "array"
    if t in ("int32", "int64", "uint8", "uint32"):
        return "int"
    if t in ("float", "double"):
        return "float"
    if t == "bool":
        return "bool"
    if t in ("FString", "FText", "FName"):
        return "text"
    if t in enums:
        return "enum"
    if t.startswith("TSoftObjectPtr<") or t.startswith("TObjectPtr<"):
        return "asset"
    return "struct"


def parse_structs(enums):
    out = {}
    for path in _iter_headers():
        for m in STRUCT_RE.finditer(_read(path)):
            fields = []
            for p in UPROP_RE.finditer(m.group("body")):
                cpp_type = p.group("type").strip()
                # TArray<EJRPGElement> -> o editor vira uma lista de opções do
                # enum em vez de um campo de texto solto.
                inner = re.fullmatch(r"TArray\s*<\s*([\w:]+)\s*>", cpp_type.replace(" ", ""))
                item_type = inner.group(1) if inner else None
                fields.append({
                    "name": p.group("name"),
                    "cppType": cpp_type,
                    "kind": _classify(cpp_type, enums),
                    "enum": cpp_type if cpp_type in enums else None,
                    "itemType": item_type,
                    "itemEnum": item_type if item_type in enums else None,
                })
            out[m.group("name")] = {
                "header": os.path.relpath(path, PLUGIN_DIR).replace("\\", "/"),
                "fields": fields,
            }
    return out


_schema_cache = {"stamp": None, "enums": None, "structs": None}


def schema():
    """Schema do C++, recarregado quando algum header muda (mtime mais novo)."""
    newest = 0.0
    for p in _iter_headers():
        try:
            newest = max(newest, os.path.getmtime(p))
        except OSError:
            pass
    if _schema_cache["stamp"] != newest:
        enums = parse_enums()
        _schema_cache.update(stamp=newest, enums=enums, structs=parse_structs(enums))
    return _schema_cache["enums"], _schema_cache["structs"]


# ========================================================================
# CSV
# ========================================================================

def csv_path(table_id):
    return os.path.join(CSV_DIR, table_id + ".csv")


def read_csv_table(table_id):
    """Lê o CSV. A 1ª coluna do header é '---' (o RowName da DataTable)."""
    path = csv_path(table_id)
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8-sig")
    rows = list(csv.reader(io.StringIO(text)))
    if not rows:
        return {"columns": [], "rows": [], "newline": "\r\n"}
    return {
        "columns": ["RowName"] + rows[0][1:],
        "rows": rows[1:],
        # Preserva o fim de linha do arquivo: DT_Items/DT_Shops são CRLF e
        # DT_CameraPresets é LF. Reescrever com outro produziria um diff
        # gigante e inútil no controle de versão.
        "newline": "\r\n" if b"\r\n" in raw else "\n",
    }


def write_csv_table(table_id, columns, rows, newline="\r\n"):
    """Grava o CSV com backup do anterior. Devolve o caminho do backup."""
    path = csv_path(table_id)
    backup = None
    if os.path.isfile(path):
        os.makedirs(BACKUP_DIR, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        backup = os.path.join(BACKUP_DIR, "%s.%s.csv" % (table_id, stamp))
        with open(path, "rb") as src, open(backup, "wb") as dst:
            dst.write(src.read())

    header = ["---"] + list(columns[1:])
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator=newline)
    w.writerow(header)
    for r in rows:
        w.writerow(r)

    # Escreve em arquivo temporário e troca: se cair energia no meio, o CSV
    # antigo continua íntegro em vez de virar meio arquivo.
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="") as f:
        f.write(buf.getvalue())
    os.replace(tmp, path)
    return backup


def uasset_path(table_id):
    root = CONFIG.get("unreal_project") or ""
    if not root or not os.path.isdir(root):
        return None
    for cur, _dirs, files in os.walk(os.path.join(root, "Content")):
        for fn in files:
            if fn == table_id + ".uasset":
                return os.path.join(cur, fn)
    return None


def parse_unreal_array(cell):
    """'(a,b,c)' -> ['a','b','c']. Vazio ou '()' -> []."""
    cell = (cell or "").strip().strip('"').strip()
    if not cell.startswith("(") or not cell.endswith(")"):
        return []
    inner = cell[1:-1].strip()
    if not inner:
        return []
    return [p.strip().strip('"') for p in inner.split(",") if p.strip()]


def make_unreal_array(values):
    values = [v for v in (values or []) if str(v).strip()]
    return "(%s)" % ",".join(str(v).strip() for v in values) if values else ""


# ========================================================================
# VALIDAÇÃO
# ========================================================================

def row_names(table_id):
    data = read_csv_table(table_id)
    if not data:
        return set()
    return {r[0].strip() for r in data["rows"] if r and r[0].strip()}


def references_to(table_id, row_name):
    """Onde este RowName é citado. Usado antes de apagar."""
    hits = []
    for (src_table, field), target in REFERENCES.items():
        if target != table_id:
            continue
        data = read_csv_table(src_table)
        if not data or field not in data["columns"]:
            continue
        idx = data["columns"].index(field)
        for r in data["rows"]:
            if idx < len(r) and row_name in parse_unreal_array(r[idx]):
                hits.append({"table": src_table, "row": r[0].strip(), "field": field})
    return hits


def validate():
    findings = []
    _enums, structs = schema()

    items = read_csv_table("DT_Items")
    shops = read_csv_table("DT_Shops")
    item_keys = row_names("DT_Items") if items else set()

    if shops and items:
        cols = shops["columns"]
        for field in ("Inventory", "FeaturedItems"):
            if field not in cols:
                continue
            idx = cols.index(field)
            for r in shops["rows"]:
                if idx >= len(r):
                    continue
                for key in parse_unreal_array(r[idx]):
                    if key not in item_keys:
                        findings.append({"level": "error", "table": "DT_Shops",
                                         "row": r[0].strip(), "field": field,
                                         "message": "item '%s' não existe em DT_Items" % key})

        if "Inventory" in cols and "FeaturedItems" in cols:
            i_inv, i_feat = cols.index("Inventory"), cols.index("FeaturedItems")
            for r in shops["rows"]:
                if max(i_inv, i_feat) >= len(r):
                    continue
                inv = set(parse_unreal_array(r[i_inv]))
                for key in parse_unreal_array(r[i_feat]):
                    if key not in inv:
                        findings.append({"level": "warn", "table": "DT_Shops",
                                         "row": r[0].strip(), "field": "FeaturedItems",
                                         "message": "'%s' está em FeaturedItems mas não no Inventory" % key})

    for t in TABLES:
        data = read_csv_table(t["id"])
        if not data:
            continue
        seen = {}
        for r in data["rows"]:
            if r and r[0].strip():
                seen[r[0].strip()] = seen.get(r[0].strip(), 0) + 1
        for name, n in seen.items():
            if n > 1:
                findings.append({"level": "error", "table": t["id"], "row": name,
                                 "field": "RowName", "message": "RowName repetido %d vezes" % n})

        sch = structs.get(t["struct"])
        if not sch:
            continue
        csv_cols = data["columns"][1:]
        cpp_cols = [f["name"] for f in sch["fields"]]
        for c in csv_cols:
            if c and c not in cpp_cols:
                findings.append({"level": "warn", "table": t["id"], "row": "-", "field": c,
                                 "message": "coluna existe no CSV mas não em %s" % t["struct"]})
        for c in cpp_cols:
            if c not in csv_cols:
                findings.append({"level": "warn", "table": t["id"], "row": "-", "field": c,
                                 "message": "campo de %s não tem coluna no CSV (importa como default)" % t["struct"]})

    return findings


# ========================================================================
# BUILDS (subprocess com log ao vivo por polling)
# ========================================================================

class Job:
    def __init__(self, key):
        self.key = key
        self.lines = []
        self.done = False
        self.code = None
        self.lock = threading.Lock()

    def append(self, text):
        with self.lock:
            self.lines.append(text)

    def snapshot(self, start):
        with self.lock:
            return self.lines[start:], len(self.lines), self.done, self.code


JOBS = {}
JOBS_LOCK = threading.Lock()


def start_build(key):
    spec = BUILDS.get(key)
    if not spec:
        return None, "build desconhecido: %s" % key

    with JOBS_LOCK:
        running = [j for j in JOBS.values() if not j.done]
        if running:
            return None, "já existe um build rodando (%s)" % running[0].key
        job = Job(key)
        JOBS[key] = job

    script = os.path.join(EXTRAS_DIR, spec["script"])

    def run():
        job.append("$ python %s\n" % os.path.relpath(script, PLUGIN_DIR))
        try:
            proc = subprocess.Popen(
                [sys.executable, "-u", script],
                cwd=PLUGIN_DIR,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                # O build_plugin.py pergunta antes de apagar a pasta de build;
                # sem stdin ele receberia EOF e abortaria.
                stdin=subprocess.DEVNULL,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
            )
            for line in proc.stdout:
                job.append(line)
            proc.wait()
            job.code = proc.returncode
        except Exception as e:      # noqa: BLE001 - qualquer falha vira log
            job.append("\n[studio] ERRO ao executar: %s\n" % e)
            job.code = -1
        finally:
            job.done = True
            job.append("\n[studio] terminou com código %s\n" % job.code)

    threading.Thread(target=run, daemon=True).start()
    return job, None


# ========================================================================
# PAYLOADS
# ========================================================================

def build_status():
    enums, structs = schema()
    unreal_root = CONFIG.get("unreal_project") or ""
    unreal_ok = bool(unreal_root) and os.path.isdir(unreal_root)

    tables = []
    for t in TABLES:
        data = read_csv_table(t["id"])
        sch = structs.get(t["struct"])
        asset = uasset_path(t["id"]) if unreal_ok else None
        entry = dict(t)
        entry.update({
            "hasStruct": sch is not None,
            "header": sch["header"] if sch else None,
            "fieldCount": len(sch["fields"]) if sch else 0,
            "hasCsv": data is not None,
            "rowCount": len(data["rows"]) if data else 0,
            "columnCount": len(data["columns"]) if data else 0,
            "hasAsset": asset is not None,
            "assetPath": os.path.relpath(asset, unreal_root).replace("\\", "/") if asset else None,
        })
        tables.append(entry)

    return {
        "pluginDir": PLUGIN_DIR.replace("\\", "/"),
        "unrealProject": unreal_root.replace("\\", "/"),
        "unrealOk": unreal_ok,
        "tables": tables,
        "pending": [{"name": n, "inPlugin": os.path.isfile(os.path.join(GAMEDATA_DIR, n + ".toml"))}
                    for n in KNOWN_TOML_ONLY],
        "builds": [{"key": k, **v} for k, v in BUILDS.items()],
        "enums": enums,
        "findings": validate(),
    }


def table_payload(table_id):
    data = read_csv_table(table_id)
    if data is None:
        return None
    _enums, structs = schema()
    meta = next(t for t in TABLES if t["id"] == table_id)
    data["id"] = table_id
    data["struct"] = meta["struct"]
    data["schema"] = structs.get(meta["struct"])
    data["meta"] = meta
    data["refs"] = {f: target for (tid, f), target in REFERENCES.items() if tid == table_id}
    return data


# ========================================================================
# SERVIDOR
# ========================================================================

MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
        ".js": "application/javascript; charset=utf-8", ".json": "application/json; charset=utf-8",
        ".ttf": "font/ttf", ".png": "image/png", ".webp": "image/webp", ".svg": "image/svg+xml"}


class StudioHandler(BaseHTTPRequestHandler):
    server_version = "LegaiaStudio"

    def log_message(self, fmt, *args):
        msg = fmt % args
        if "/api/" in msg and "/log?" not in msg:      # o polling do log poluiria
            sys.stderr.write("[studio] %s\n" % msg)

    def _send(self, code, body, content_type="text/plain; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, data, code=200):
        self._send(code, json.dumps(data, ensure_ascii=False), MIME[".json"])

    def _file(self, path):
        if not os.path.isfile(path):
            return self._send(404, "404 — não encontrado")
        with open(path, "rb") as f:
            self._send(200, f.read(), MIME.get(os.path.splitext(path)[1].lower(),
                                               "application/octet-stream"))

    def _body(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
            return json.loads(self.rfile.read(n).decode("utf-8")) if n else {}
        except (ValueError, UnicodeDecodeError):
            return None

    # --- GET ---

    def do_GET(self):
        raw = self.path.split("?", 1)
        path, query = raw[0], (raw[1] if len(raw) > 1 else "")

        if path == "/":
            path = "/index.html"
        if path.startswith("/api/"):
            return self._api_get(path[5:], query)
        if path.startswith("/fonts/"):
            return self._file(os.path.join(FONTS_DIR, os.path.basename(path)))

        rel = os.path.normpath(path.lstrip("/")).replace("\\", "/")
        if rel.startswith(".."):
            return self._send(403, "403")
        return self._file(os.path.join(WEB_DIR, rel))

    def _api_get(self, route, query):
        if route == "status":
            return self._json(build_status())
        if route == "validate":
            return self._json({"findings": validate()})

        if route.startswith("table/"):
            data = table_payload(route[len("table/"):])
            return self._json(data) if data else self._json({"error": "tabela não encontrada"}, 404)

        m = re.fullmatch(r"build/(\w+)/log", route)
        if m:
            job = JOBS.get(m.group(1))
            if not job:
                return self._json({"error": "sem build iniciado"}, 404)
            start = 0
            for part in query.split("&"):
                if part.startswith("from="):
                    try:
                        start = int(part[5:])
                    except ValueError:
                        start = 0
            lines, total, done, code = job.snapshot(start)
            return self._json({"lines": lines, "next": total, "done": done, "code": code})

        return self._json({"error": "rota desconhecida: %s" % route}, 404)

    # --- POST ---

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if not path.startswith("/api/"):
            return self._send(404, "404")
        route = path[5:]
        body = self._body()
        if body is None:
            return self._json({"error": "JSON inválido"}, 400)

        m = re.fullmatch(r"table/([\w]+)/save", route)
        if m:
            return self._save_table(m.group(1), body)

        if route == "reveal":
            return self._reveal(body)

        m = re.fullmatch(r"build/(\w+)", route)
        if m:
            job, err = start_build(m.group(1))
            if err:
                return self._json({"error": err}, 409)
            return self._json({"started": True, "key": job.key})

        return self._json({"error": "rota desconhecida: %s" % route}, 404)

    def _reveal(self, body):
        """Abre uma das pastas conhecidas no explorador de arquivos.

        Só aceita apelidos ('plugin' / 'unreal'), nunca um caminho vindo do
        cliente: assim a rota não vira um "abra qualquer coisa nesta máquina".
        """
        targets = {
            "plugin": PLUGIN_DIR,
            "unreal": CONFIG.get("unreal_project") or "",
            "data": CSV_DIR,
        }
        path = targets.get(body.get("target"))
        if not path or not os.path.isdir(path):
            return self._json({"error": "pasta não encontrada"}, 404)

        try:
            if sys.platform.startswith("win"):
                os.startfile(path)                                  # noqa: S606
            elif sys.platform == "darwin":
                subprocess.Popen(["open", path])
            else:
                subprocess.Popen(["xdg-open", path])
        except OSError as e:
            return self._json({"error": str(e)}, 500)
        return self._json({"opened": path})

    def _save_table(self, table_id, body):
        if table_id not in [t["id"] for t in TABLES]:
            return self._json({"error": "tabela desconhecida"}, 404)

        current = read_csv_table(table_id)
        if current is None:
            return self._json({"error": "CSV não encontrado"}, 404)

        rows = body.get("rows")
        if not isinstance(rows, list):
            return self._json({"error": "corpo sem 'rows'"}, 400)

        ncols = len(current["columns"])
        clean = []
        seen = set()
        for r in rows:
            if not isinstance(r, list):
                return self._json({"error": "linha não é lista"}, 400)
            r = [("" if c is None else str(c)) for c in r]
            r = (r + [""] * ncols)[:ncols]
            name = r[0].strip()
            if not name:
                return self._json({"error": "há linha sem RowName"}, 400)
            if name in seen:
                return self._json({"error": "RowName repetido: %s" % name}, 400)
            seen.add(name)
            clean.append(r)

        backup = write_csv_table(table_id, current["columns"], clean, current["newline"])
        return self._json({
            "saved": True,
            "rowCount": len(clean),
            "backup": os.path.relpath(backup, PLUGIN_DIR).replace("\\", "/") if backup else None,
            "findings": validate(),
        })


def main():
    ap = argparse.ArgumentParser(description="LegaiaStudio")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(WEB_DIR):
        print("[studio] ERRO: pasta web/ não encontrada em %s" % WEB_DIR)
        return 1

    server = ThreadingHTTPServer(("127.0.0.1", args.port), StudioHandler)
    url = "http://127.0.0.1:%d/" % args.port
    unreal = CONFIG.get("unreal_project") or ""

    print("LegaiaStudio")
    print("  plugin : %s" % PLUGIN_DIR)
    print("  unreal : %s%s" % (unreal, "" if os.path.isdir(unreal) else "   (não encontrado)"))
    print("  url    : %s" % url)
    print("  Ctrl+C para parar.\n")

    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[studio] encerrado.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
