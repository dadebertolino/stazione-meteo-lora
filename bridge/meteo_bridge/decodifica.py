"""Dalla riga CSV del ricevitore alla misura.

  id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr

Le righe che iniziano con '#' e quelle del core RUI3 ('+EVT:...',
'Current Work Mode...') non sono dati. I campi vuoti sono misure non
disponibili e diventano None.
"""

from dataclasses import asdict, dataclass
from typing import Optional

NUM_CAMPI = 11
TIPI = ("bme280", "sht45")


@dataclass(frozen=True)
class Misura:
    nodo: int
    tipo: str
    seq: int
    reset: Optional[int]
    temp_c: Optional[float]
    ur_pct: Optional[float]
    press_hpa: Optional[float]
    vbat_mv: Optional[int]
    vbat_tx_mv: Optional[int]
    rssi: int
    snr: int

    def come_dict(self):
        return asdict(self)


def _int(s):
    return int(s) if s else None


def _float(s):
    return float(s) if s else None


def decodifica(riga):
    """La Misura della riga, o None se la riga non è di dati."""
    riga = riga.strip()
    if not riga or riga[0] in "#+":
        return None
    campi = riga.split(",")
    if len(campi) != NUM_CAMPI:
        return None
    try:
        m = Misura(
            nodo=int(campi[0]),
            seq=int(campi[1]),
            tipo=campi[2],
            reset=_int(campi[3]),
            temp_c=_float(campi[4]),
            ur_pct=_float(campi[5]),
            press_hpa=_float(campi[6]),
            vbat_mv=_int(campi[7]),
            vbat_tx_mv=_int(campi[8]),
            rssi=int(campi[9]),
            snr=int(campi[10]),
        )
    except ValueError:
        return None
    # Il ricevitore stampa solo pacchetti con CRC valido: questi controlli
    # scartano righe rovinate sulla seriale, non misure strane
    if not (1 <= m.nodo <= 15 and 0 <= m.seq <= 255 and m.tipo in TIPI):
        return None
    return m
