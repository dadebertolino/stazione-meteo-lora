"""Test del bridge, senza ricevitore:  python3 -m unittest discover tests

Le righe vengono dal ricevitore vero (prove del 25/10/2026).
"""

import unittest
from datetime import datetime, timedelta, timezone

from meteo_bridge.coda import DA_INVIARE, INVIATA, RIFIUTATA, Coda, ora_utc
from meteo_bridge.decodifica import decodifica


class TestDecodifica(unittest.TestCase):

    def test_riga_normale(self):
        m = decodifica("2,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14")
        self.assertEqual((m.nodo, m.seq, m.tipo, m.reset), (2, 41, "bme280", None))
        self.assertEqual((m.temp_c, m.ur_pct, m.press_hpa), (18.33, 57.24, 964.7))
        self.assertEqual((m.vbat_mv, m.vbat_tx_mv, m.rssi, m.snr), (3000, 3000, -51, 14))

    def test_primo_pacchetto_dopo_riavvio(self):
        m = decodifica("2,0,bme280,1,20.73,50.16,965.8,3000,,-23,13")
        self.assertEqual((m.seq, m.reset, m.vbat_tx_mv), (0, 1, None))

    def test_sensore_guasto(self):
        m = decodifica("2,5,bme280,,,,,3000,2980,-50,12")
        self.assertIsNone(m.temp_c)
        self.assertIsNone(m.ur_pct)
        self.assertIsNone(m.press_hpa)
        self.assertEqual(m.vbat_mv, 3000)

    def test_nodo_sht45_senza_pressione(self):
        m = decodifica("1,7,sht45,,12.50,80.10,,2900,2870,-70,8")
        self.assertEqual(m.tipo, "sht45")
        self.assertIsNone(m.press_hpa)

    def test_temperatura_negativa(self):
        self.assertEqual(decodifica("2,9,bme280,,-7.25,90.00,970.1,2800,2750,-60,10").temp_c, -7.25)

    def test_fine_riga_windows(self):
        self.assertIsNotNone(decodifica("2,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14\r\n"))

    def test_righe_non_di_dati(self):
        for riga in (
            "",
            "# id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr",
            "# nodo 2: riavvio (causa: alimentazione)",
            "# stat: 40 validi, 0 CRC errato, 0 errori radio, 0 formato, 0 coda piena, 0 troppo lunghi",
            "# pacchetto scartato: CRC errato (24 byte, RSSI -110)",
            "+EVT:RXP2P:-51:14:1229100729165C25AF0BB80BB8619B",
            "+EVT:RXP2P RECEIVE ERROR",
            "Current Work Mode: LoRa P2P.",
            "OK",
        ):
            self.assertIsNone(decodifica(riga), riga)

    def test_righe_rovinate(self):
        for riga in (
            "2,41,bme280,,18.33,57.24,964.7,3000,3000,-51",         # campo mancante
            "2,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14,0",    # campo in più
            "2,41,bme280,,18.3x,57.24,964.7,3000,3000,-51,14",      # numero rovinato
            "0,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14",      # nodo 0
            "16,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14",     # nodo oltre 15
            "2,256,bme280,,18.33,57.24,964.7,3000,3000,-51,14",     # seq oltre 255
            "2,41,bme281,,18.33,57.24,964.7,3000,3000,-51,14",      # tipo sconosciuto
        ):
            self.assertIsNone(decodifica(riga), riga)


class TestCoda(unittest.TestCase):

    def setUp(self):
        self.coda = Coda(":memory:")
        self.t0 = datetime(2026, 10, 25, 17, 42, 7, tzinfo=timezone.utc)

    def tearDown(self):
        self.coda.chiudi()

    def misura(self, seq):
        return decodifica("2,%d,bme280,,18.33,57.24,964.7,3000,3000,-51,14" % seq)

    def test_ora_in_utc(self):
        roma = timezone(timedelta(hours=1))
        self.assertEqual(ora_utc(datetime(2026, 10, 25, 18, 42, 7, tzinfo=roma)), "2026-10-25T17:42:07Z")

    def test_aggiungi_e_doppione(self):
        self.assertTrue(self.coda.aggiungi(self.misura(1), self.t0))
        self.assertFalse(self.coda.aggiungi(self.misura(1), self.t0))
        self.assertEqual(self.coda.conta(), 1)

    def test_da_inviare_dalle_piu_vecchie(self):
        for i in range(3):
            self.coda.aggiungi(self.misura(i + 1), self.t0 + timedelta(seconds=30 * i))
        lotto = self.coda.da_inviare(limite=2)
        self.assertEqual([d["seq"] for _, d in lotto], [1, 2])
        self.assertEqual(lotto[0][1]["ricevuto"], "2026-10-25T17:42:07Z")
        self.assertIsNone(lotto[0][1]["reset"])

    def test_segna(self):
        for i in range(3):
            self.coda.aggiungi(self.misura(i + 1), self.t0 + timedelta(seconds=30 * i))
        ids = [i for i, _ in self.coda.da_inviare()]
        self.coda.segna(ids[:2], INVIATA)
        self.coda.segna(ids[2:], RIFIUTATA)
        self.assertEqual(self.coda.conta(DA_INVIARE), 0)
        self.assertEqual(self.coda.conta(INVIATA), 2)
        self.assertEqual(self.coda.conta(RIFIUTATA), 1)
        self.assertEqual(self.coda.da_inviare(), [])


if __name__ == "__main__":
    unittest.main()
