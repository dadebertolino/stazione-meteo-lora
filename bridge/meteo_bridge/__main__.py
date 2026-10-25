"""Avvio del bridge:  python3 -m meteo_bridge --config /etc/meteo-bridge/config.toml

Due attività nello stesso processo:
  1. lettura della seriale: ogni riga di dati, con data e ora, va nella coda
  2. invio: subito e poi ogni N minuti le misure non ancora inviate partono
     verso il sito (senza indirizzo del sito restano in coda)

Prima di registrare qualcosa aspetta che l'orologio sia sincronizzato (NTP):
il Raspberry non ha un orologio a batteria.

Per le prove, senza file di configurazione:
  python3 -m meteo_bridge --porta /dev/cu.usbserial-XXXX --database coda.sqlite \\
      --url http://sito.local/wp-json/dbmt/v1/misure --token ... --intervallo 60
"""

import argparse
import logging
import os
import signal
import sys
import threading
from datetime import datetime, timezone

from . import __version__
from .coda import Coda
from .decodifica import decodifica
from .invio import Inviatore, indirizzo_sicuro
from .orologio import attendi_sincronizzazione

log = logging.getLogger("meteo_bridge")


def carica_config(percorso):
    if percorso and os.path.exists(percorso):
        import tomllib  # Python 3.11+: serve solo con un file di configurazione
        with open(percorso, "rb") as f:
            return tomllib.load(f)
    return {}


def ferma(signum, frame):
    raise KeyboardInterrupt


def main():
    ap = argparse.ArgumentParser(prog="meteo_bridge", description=__doc__.splitlines()[0])
    ap.add_argument("--config", default="/etc/meteo-bridge/config.toml")
    ap.add_argument("--porta", help="porta seriale (sostituisce quella della configurazione)")
    ap.add_argument("--database", help="file della coda SQLite (sostituisce quello della configurazione)")
    ap.add_argument("--url", help="indirizzo dell'endpoint del sito (sostituisce [sito] url)")
    ap.add_argument("--token", help="token del sito (sostituisce [sito] token)")
    ap.add_argument("--intervallo", type=int, help="secondi fra due invii (sostituisce [sito] intervallo_invio_s)")
    ap.add_argument("--version", action="version", version=__version__)
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    cfg = carica_config(args.config)
    sito = cfg.get("sito", {})
    porta = args.porta or cfg.get("seriale", {}).get("porta")
    baud = cfg.get("seriale", {}).get("baud", 115200)
    database = args.database or cfg.get("coda", {}).get("database", "coda.sqlite")
    url = args.url or sito.get("url", "")
    token = args.token or sito.get("token", "")
    intervallo = args.intervallo or sito.get("intervallo_invio_s", 300)
    if not porta:
        ap.error("manca la porta seriale: --porta o [seriale] porta nella configurazione")
    if url and not token:
        ap.error("c'è l'indirizzo del sito ma manca il token")
    if url and not indirizzo_sicuro(url):
        ap.error("l'indirizzo del sito deve essere https (http solo per localhost e .local)")

    from .seriale import righe  # qui: serve pyserial, i test non lo richiedono

    signal.signal(signal.SIGTERM, ferma)  # arresto da systemd: come Ctrl+C
    coda = Coda(database)
    log.info("meteo_bridge %s: coda %s, %d misure da inviare", __version__, database, coda.conta())
    attendi_sincronizzazione()
    fermo = threading.Event()
    if url:
        log.info("invio a %s ogni %d s", url, intervallo)
        Inviatore(url, token, database).avvia(intervallo, fermo)
    else:
        log.warning("nessun indirizzo del sito: le misure restano in coda")
    try:
        for riga in righe(porta, baud):
            ricevuto = datetime.now(timezone.utc).replace(microsecond=0)
            misura = decodifica(riga)
            if misura is None:
                print(riga, flush=True)
                continue
            nuova = coda.aggiungi(misura, ricevuto)
            print("%s  [%s, in coda: %d]" % (riga, "accodata" if nuova else "doppione",
                                             coda.conta()), flush=True)
    except KeyboardInterrupt:
        log.info("interrotto: %d misure da inviare restano in coda", coda.conta())
    finally:
        fermo.set()
        coda.chiudi()
    return 0


if __name__ == "__main__":
    sys.exit(main())
