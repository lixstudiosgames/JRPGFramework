"""
legaia_toml.py — o que os geradores de DataTable compartilham: ler os TOMLs do
legend-of-legaia-re e escrever CSV no formato que o Unreal importa.

Lê com o tomllib da biblioteca padrão (Python 3.11+). Os geradores antigos têm cada um o seu
parser feito à mão, que quebra em hex ("0x29" virava string), em inline tables
(`attacks = [{ name = "...", mp = 70 }]`) e em arrays de várias linhas; o tomllib lê o TOML
inteiro como o crate gamedata do RE lê.

Formato do CSV (o mesmo de DT_Items/DT_Shops/DT_Characters, e o que o LegaiaStudio preserva):
  - cabeçalho com "---" na primeira coluna (o RowName);
  - CRLF, UTF-8 sem BOM;
  - array de FName/enum: (a,b,c) — sem aspas; array de FString: ("a","b") — aspas em cada
    item (ARCHITECTURE.md, regra 32);
  - struct: (Campo=valor,...) — "()" quando vazio, porque uma célula vazia não importa.
"""

import csv
import datetime
import os
import shutil
import sys

try:
    import tomllib
except ImportError:  # pragma: no cover
    sys.exit("Este gerador precisa do Python 3.11+ (tomllib).")

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PLUGIN_DIR, "Docs", "Data")
GAMEDATA_DIR = os.path.join(DATA_DIR, "gamedata")
BACKUP_DIR = os.path.join(DATA_DIR, "_backup")


def load(name):
    """Docs/Data/gamedata/<name>.toml como dict."""
    with open(os.path.join(GAMEDATA_DIR, name + ".toml"), "rb") as f:
        return tomllib.load(f)


def ue_array(values, quote=False):
    if quote:
        return "(" + ",".join('"%s"' % v for v in values) + ")"
    return "(" + ",".join(str(v) for v in values) + ")"


def ue_bool(value):
    return "True" if value else "False"


def csv_path(table):
    return os.path.join(DATA_DIR, table + ".csv")


def read_rows(table):
    """Linhas do CSV atual por RowName, como dict coluna -> valor. {} se não existe."""
    path = csv_path(table)
    if not os.path.isfile(path):
        return {}, []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    if not rows:
        return {}, []
    header = rows[0]
    out = {}
    order = []
    for r in rows[1:]:
        if not r:
            continue
        rec = dict(zip(header, r))
        out[r[0]] = rec
        order.append(r[0])
    return out, order


def backup(table):
    """Copia o CSV atual para Docs/Data/_backup/<table>.<timestamp>.csv (o padrão do Studio)."""
    path = csv_path(table)
    if not os.path.isfile(path):
        return None
    os.makedirs(BACKUP_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    dst = os.path.join(BACKUP_DIR, "%s.%s.csv" % (table, stamp))
    shutil.copy2(path, dst)
    return dst


def write_rows(table, columns, rows):
    """columns: nomes das colunas SEM o RowName; rows: lista de dicts com "---" + colunas."""
    with open(csv_path(table), "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\r\n")
        w.writerow(["---"] + columns)
        for r in rows:
            w.writerow([r["---"]] + [r.get(c, "") for c in columns])
