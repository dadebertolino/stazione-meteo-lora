"""Coda locale delle misure, in SQLite.

Ogni misura entra con la sua ora di arrivo e resta "da inviare" finché il
sito non conferma. Senza internet o con il sito fermo non si perde niente:
le misure partono più tardi, con l'ora originale.

L'ora è in UTC (ISO 8601 con 'Z'), come la vuole il plugin: in ora locale
l'ora ripetuta del cambio d'ora di ottobre darebbe falsi doppioni.
"""

import sqlite3
from datetime import timezone

DA_INVIARE, INVIATA, RIFIUTATA = 0, 1, 2

CAMPI = ("nodo", "tipo", "seq", "reset", "temp_c", "ur_pct", "press_hpa",
         "vbat_mv", "vbat_tx_mv", "rssi", "snr")

SCHEMA = """
CREATE TABLE IF NOT EXISTS misure (
    id         INTEGER PRIMARY KEY,
    ricevuto   TEXT    NOT NULL,
    nodo       INTEGER NOT NULL,
    tipo       TEXT    NOT NULL,
    seq        INTEGER NOT NULL,
    reset      INTEGER,
    temp_c     REAL,
    ur_pct     REAL,
    press_hpa  REAL,
    vbat_mv    INTEGER,
    vbat_tx_mv INTEGER,
    rssi       INTEGER NOT NULL,
    snr        INTEGER NOT NULL,
    stato      INTEGER NOT NULL DEFAULT 0,
    UNIQUE (nodo, ricevuto)
);
CREATE INDEX IF NOT EXISTS misure_stato ON misure (stato, id);
"""


def ora_utc(dt):
    """datetime con fuso orario → '2026-10-25T17:42:07Z'."""
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Coda:
    def __init__(self, percorso):
        # Due connessioni (lettura della seriale e invio): chi trova il file
        # occupato aspetta invece di fallire
        self.db = sqlite3.connect(percorso, timeout=10)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)

    def aggiungi(self, misura, ricevuto):
        """Accoda la misura; False se c'era già (stesso nodo, stessa ora)."""
        valori = misura.come_dict()
        cur = self.db.execute(
            "INSERT OR IGNORE INTO misure (ricevuto, %s) VALUES (?, %s)"
            % (", ".join(CAMPI), ", ".join("?" * len(CAMPI))),
            [ora_utc(ricevuto)] + [valori[c] for c in CAMPI],
        )
        self.db.commit()
        return cur.rowcount == 1

    def da_inviare(self, limite=500):
        """Le misure non ancora inviate, dalle più vecchie: [(id, dict)]."""
        righe = self.db.execute(
            "SELECT * FROM misure WHERE stato = ? ORDER BY id LIMIT ?",
            (DA_INVIARE, limite),
        ).fetchall()
        return [(r["id"], {k: r[k] for k in ("ricevuto",) + CAMPI}) for r in righe]

    def segna(self, ids, stato):
        self.db.executemany("UPDATE misure SET stato = ? WHERE id = ?",
                            [(stato, i) for i in ids])
        self.db.commit()

    def conta(self, stato=DA_INVIARE):
        return self.db.execute("SELECT COUNT(*) FROM misure WHERE stato = ?",
                               (stato,)).fetchone()[0]

    def chiudi(self):
        self.db.close()
