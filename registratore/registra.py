#!/usr/bin/env python3
"""Registra la seriale del ricevitore LoRa con l'ora di arrivo di ogni riga.

Per i test sul campo, in attesa del bridge vero. Scrive due file al giorno:
  AAAA-MM-GG.log  tutte le righe (anche quelle '#'), precedute dall'ora
  AAAA-MM-GG.csv  solo le righe di dati, con l'ora di arrivo come prima colonna

Se la porta sparisce (cavo staccato, ricevitore riavviato) riprova ogni 5 s.
L'ora è quella del computer: deve essere sincronizzata (NTP).

Uso:  python3 registra.py /dev/ttyUSB0
      python3 registra.py COM5 --cartella D:\\test-lora
Richiede pyserial (pip install pyserial).
"""

import argparse
import datetime
import os
import signal
import sys
import time

import serial

CSV_HEADER = "ricevuto,id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr"
CSV_FIELDS = CSV_HEADER.count(",")  # campi dopo "ricevuto"


def append(path, line, header=None):
    new = not os.path.exists(path)
    with open(path, "a", encoding="utf-8") as f:
        if new and header:
            f.write(header + "\n")
        f.write(line + "\n")


def record(folder, line):
    now = datetime.datetime.now().astimezone()
    stamp = now.isoformat(timespec="seconds")
    day = now.strftime("%Y-%m-%d")
    append(os.path.join(folder, day + ".log"), stamp + " " + line)
    if line and not line.startswith("#") and line.count(",") == CSV_FIELDS - 1:
        append(os.path.join(folder, day + ".csv"), stamp + "," + line, CSV_HEADER)
    print(stamp, line, flush=True)


def stop(signum, frame):
    raise KeyboardInterrupt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("porta", help="porta seriale del ricevitore (es. /dev/ttyUSB0, COM5)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cartella", default="registrazioni", help="dove salvare i file")
    args = ap.parse_args()

    os.makedirs(args.cartella, exist_ok=True)
    signal.signal(signal.SIGTERM, stop)  # arresto da servizio di sistema: come Ctrl+C
    missing = False  # registra solo i cambi di stato, non ogni tentativo
    try:
        while True:
            try:
                with serial.Serial(args.porta, args.baud, timeout=1) as port:
                    missing = False
                    record(args.cartella, "# registratore: porta %s aperta" % args.porta)
                    while True:
                        raw = port.readline()
                        if raw:
                            record(args.cartella, raw.decode("utf-8", errors="replace").rstrip("\r\n"))
            except serial.SerialException as e:
                if not missing:
                    record(args.cartella, "# registratore: porta non disponibile (%s), riprovo ogni 5 s" % e)
                    missing = True
                time.sleep(5)
    except KeyboardInterrupt:
        # Fuori dal ciclo: arriva anche mentre si aspetta la porta
        record(args.cartella, "# registratore: interrotto")
        return 0


if __name__ == "__main__":
    sys.exit(main())
