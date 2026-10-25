"""Avvio del bridge:  python3 -m meteo_bridge --config /etc/meteo-bridge/config.toml

Due attività nello stesso processo:
  1. lettura della seriale: ogni riga di dati, con data e ora, va nella coda
  2. invio: ogni N minuti le misure non ancora inviate partono verso il sito

Prima di registrare qualcosa aspetta che l'orologio sia sincronizzato (NTP):
il Raspberry non ha un orologio a batteria.
"""

import argparse
import sys

from . import __version__


def main():
    ap = argparse.ArgumentParser(prog="meteo_bridge", description=__doc__.splitlines()[0])
    ap.add_argument("--config", default="/etc/meteo-bridge/config.toml")
    ap.add_argument("--version", action="version", version=__version__)
    ap.parse_args()
    print("meteo_bridge %s: struttura, non ancora operativo" % __version__, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
