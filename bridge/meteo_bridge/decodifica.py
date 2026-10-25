"""Dalla riga CSV del ricevitore alla misura.

  id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr

Le righe che iniziano con '#' e quelle del core RUI3 ('+EVT:...',
'Current Work Mode...') non sono dati. I campi vuoti sono misure non
disponibili e diventano None.
"""
