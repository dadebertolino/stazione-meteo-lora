"""Invio delle misure al plugin WordPress.

POST https://<sito>/wp-json/dbmt/v1/misure, in gruppi di al massimo 500, con
il token nell'header X-DBMT-Token. Le misure vengono segnate come inviate
solo dopo la risposta positiva del sito; i reinvii sono innocui perché il
plugin scarta le misure già presenti. Risposte: README del plugin.

Solo libreria standard (urllib): sul Raspberry non serve installare altro.
"""

import json
import logging
import threading
import urllib.error
import urllib.parse
import urllib.request

from . import __version__
from .coda import INVIATA, RIFIUTATA, Coda

log = logging.getLogger(__name__)

MAX_GRUPPO = 500
TIMEOUT_S = 30


def indirizzo_sicuro(url):
    """Il token viaggia in chiaro con http: ammesso solo in locale, per le prove."""
    p = urllib.parse.urlsplit(url)
    if p.scheme == "https":
        return True
    host = p.hostname or ""
    return p.scheme == "http" and (host in ("localhost", "127.0.0.1") or host.endswith(".local"))


class Inviatore:
    def __init__(self, url, token, database):
        self.url = url
        self.token = token
        self.database = database

    def _post(self, misure):
        """(codice HTTP, risposta JSON o None); codice None se il sito non risponde."""
        corpo = json.dumps({"bridge": __version__, "misure": misure}).encode()
        richiesta = urllib.request.Request(self.url, data=corpo, method="POST", headers={
            "Content-Type": "application/json",
            "X-DBMT-Token": self.token,
            # Alcuni firewall degli hosting bloccano l'user agent di urllib
            "User-Agent": "meteo-bridge/%s" % __version__,
        })
        try:
            with urllib.request.urlopen(richiesta, timeout=TIMEOUT_S) as r:
                return r.status, json.load(r)
        except urllib.error.HTTPError as e:
            return e.code, None
        except (urllib.error.URLError, OSError, ValueError) as e:
            log.warning("sito non raggiungibile (%s): riprovo al prossimo giro", e)
            return None, None

    def invia_coda(self, coda):
        """Svuota la coda in gruppi, dalle misure più vecchie; si ferma al primo errore."""
        while True:
            gruppo = coda.da_inviare(MAX_GRUPPO)
            if not gruppo:
                return
            ids = [i for i, _ in gruppo]
            codice, risposta = self._post([m for _, m in gruppo])

            if codice == 200 and isinstance(risposta, dict):
                rifiutate = {s.get("indice") for s in risposta.get("scartate", [])}
                for s in risposta.get("scartate", []):
                    log.error("misura rifiutata dal sito: %s (%s)", gruppo[s["indice"]][1], s.get("motivo"))
                coda.segna([i for n, i in enumerate(ids) if n not in rifiutate], INVIATA)
                coda.segna([i for n, i in enumerate(ids) if n in rifiutate], RIFIUTATA)
                log.info("inviate %d misure: %d nuove, %d doppioni, %d rifiutate; in coda %d",
                         len(ids), risposta.get("inserite", 0), risposta.get("doppioni", 0),
                         len(rifiutate), coda.conta())
                if len(gruppo) < MAX_GRUPPO:
                    return
            elif codice == 401:
                log.error("token rifiutato dal sito: va rigenerato in WordPress e copiato nella configurazione")
                return
            elif codice == 400:
                # Errore del bridge, non del sito: il gruppo non passerebbe mai e
                # bloccherebbe la coda. Resta nel database con stato "rifiutata".
                log.error("gruppo di %d misure rifiutato come non valido (400): segnato come rifiutato", len(ids))
                coda.segna(ids, RIFIUTATA)
            elif codice is not None:
                log.warning("risposta %d dal sito: riprovo al prossimo giro", codice)
                return
            else:
                return

    def avvia(self, intervallo_s, fermo):
        """Thread che invia subito e poi ogni intervallo_s, finché fermo non è impostato."""
        def ciclo():
            coda = Coda(self.database)  # connessione propria: SQLite non la condivide fra thread
            try:
                while True:
                    try:
                        self.invia_coda(coda)
                    except Exception:  # l'invio non deve mai fermare la lettura della seriale
                        log.exception("errore inatteso nell'invio")
                    if fermo.wait(intervallo_s):
                        return
            finally:
                coda.chiudi()

        t = threading.Thread(target=ciclo, name="invio", daemon=True)
        t.start()
        return t
