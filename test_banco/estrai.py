# Estrae dagli sketch le parti da provare al banco (vedi esegui.sh)
import sys
nodo, rx, out = sys.argv[1:4]
def cut(path, start, end):
    s = open(path, encoding="utf-8").read()
    i = s.index(start); j = s.index(end, i)
    return s[i:j]
n = open(nodo, encoding="utf-8").read()
parts = [
  "// ===== estratto dal nodo =====\n",
  cut(nodo, "#define NODE_ID", "#if NODE_DEBUG"),
  "namespace nodo {\n", cut(nodo, "// ---------- CRC-16", "// ---------- Bus I2C"), "}\n",
  "// ===== estratto dal ricevitore =====\n",
  "#undef FW_VERSION\n",  # nodo e ricevitore hanno versioni diverse
  "namespace rx {\n", cut(rx, "#define FW_VERSION", "// ---------- Avvio"), "}\n",
]
open(out, "w", encoding="utf-8").write("".join(parts))
