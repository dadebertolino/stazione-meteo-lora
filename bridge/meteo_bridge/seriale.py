"""Lettura della seriale del ricevitore, una riga alla volta.

Come il registratore: se la porta sparisce (cavo staccato, ricevitore
riavviato) riprova ogni 5 s. Sulla porta non si scrive mai: l'interprete
AT del ricevitore è attivo, e qualunque cosa scritta verrebbe eseguita
come comando.
"""

import logging
import time

import serial

log = logging.getLogger(__name__)

ATTESA_RIPROVA_S = 5


def righe(porta, baud=115200):
    """Generatore infinito delle righe ricevute, senza fine riga."""
    mancante = False  # registra solo i cambi di stato, non ogni tentativo
    while True:
        try:
            with serial.Serial(porta, baud, timeout=1) as s:
                mancante = False
                log.info("porta %s aperta", porta)
                while True:
                    grezza = s.readline()
                    if grezza:
                        yield grezza.decode("utf-8", errors="replace").rstrip("\r\n")
        except serial.SerialException as e:
            if not mancante:
                log.warning("porta non disponibile (%s), riprovo ogni %d s", e, ATTESA_RIPROVA_S)
                mancante = True
            time.sleep(ATTESA_RIPROVA_S)
