"""Attesa della sincronizzazione dell'orologio di sistema.

Il Raspberry 3 non ha un orologio a batteria: dopo un'interruzione di
corrente riparte con l'ora sbagliata finché NTP non la corregge. Fino ad
allora nessuna misura viene registrata, perché avrebbe la data sbagliata.

Lo stato si legge da systemd-timesyncd (timedatectl). Senza timedatectl,
per esempio su macOS durante le prove, l'orologio si considera giusto.
"""

import logging
import shutil
import subprocess
import time

log = logging.getLogger(__name__)

ATTESA_S = 10


def sincronizzato():
    """True se NTP ha sincronizzato l'orologio; None se non si può sapere."""
    if not shutil.which("timedatectl"):
        return None
    esito = subprocess.run(["timedatectl", "show", "-p", "NTPSynchronized", "--value"],
                           capture_output=True, text=True)
    return esito.stdout.strip() == "yes"


def attendi_sincronizzazione():
    stato = sincronizzato()
    if stato is None:
        log.warning("timedatectl non disponibile: considero l'orologio sincronizzato")
        return
    if stato:
        return
    log.warning("orologio non ancora sincronizzato (NTP): attendo prima di registrare")
    while not sincronizzato():
        time.sleep(ATTESA_S)
    log.info("orologio sincronizzato")
