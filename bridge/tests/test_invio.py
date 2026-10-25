"""Test dell'invio contro un server HTTP finto che risponde come il plugin."""

import json
import threading
import unittest
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

from meteo_bridge.coda import DA_INVIARE, INVIATA, RIFIUTATA, Coda
from meteo_bridge.decodifica import decodifica
from meteo_bridge.invio import MAX_GRUPPO, Inviatore, indirizzo_sicuro


class FintoSito(BaseHTTPRequestHandler):
    risposte = []      # (codice, corpo) in ordine; l'ultima si ripete
    ricevute = []      # (header token, misure) di ogni richiesta

    def do_POST(self):
        corpo = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        FintoSito.ricevute.append((self.headers.get("X-DBMT-Token"), corpo["misure"]))
        codice, risposta = FintoSito.risposte.pop(0) if len(FintoSito.risposte) > 1 else FintoSito.risposte[0]
        if callable(risposta):
            risposta = risposta(corpo["misure"])
        dati = json.dumps(risposta).encode()
        self.send_response(codice)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        self.wfile.write(dati)

    def log_message(self, *args):
        pass


def tutte_nuove(misure):
    return {"ricevute": len(misure), "inserite": len(misure), "doppioni": 0, "scartate": []}


class TestInvio(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.server = HTTPServer(("127.0.0.1", 0), FintoSito)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.url = "http://127.0.0.1:%d/wp-json/dbmt/v1/misure" % cls.server.server_port

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def setUp(self):
        FintoSito.ricevute = []
        self.coda = Coda(":memory:")
        self.inviatore = Inviatore(self.url, "segreto", ":memory:")

    def tearDown(self):
        self.coda.chiudi()

    def accoda(self, n):
        t0 = datetime(2026, 10, 25, 16, 0, 0, tzinfo=timezone.utc)
        for i in range(n):
            m = decodifica("2,%d,bme280,,18.33,57.24,964.7,3000,3000,-51,14" % (i % 256))
            self.coda.aggiungi(m, t0 + timedelta(seconds=30 * i))

    def test_invio_riuscito(self):
        self.accoda(3)
        FintoSito.risposte = [(200, tutte_nuove)]
        self.inviatore.invia_coda(self.coda)
        token, misure = FintoSito.ricevute[0]
        self.assertEqual(token, "segreto")
        self.assertEqual(len(misure), 3)
        self.assertEqual(misure[0]["ricevuto"], "2026-10-25T16:00:00Z")
        self.assertEqual(self.coda.conta(INVIATA), 3)
        self.assertEqual(self.coda.conta(DA_INVIARE), 0)

    def test_misura_scartata_dal_sito(self):
        self.accoda(3)
        FintoSito.risposte = [(200, {"ricevute": 3, "inserite": 2, "doppioni": 0,
                                     "scartate": [{"indice": 1, "motivo": "temp_c fuori campo"}]})]
        with self.assertLogs("meteo_bridge.invio", "ERROR"):
            self.inviatore.invia_coda(self.coda)
        self.assertEqual(self.coda.conta(INVIATA), 2)
        self.assertEqual(self.coda.conta(RIFIUTATA), 1)

    def test_coda_lunga_in_piu_gruppi(self):
        self.accoda(MAX_GRUPPO + 20)
        FintoSito.risposte = [(200, tutte_nuove)]
        self.inviatore.invia_coda(self.coda)
        self.assertEqual([len(m) for _, m in FintoSito.ricevute], [MAX_GRUPPO, 20])
        self.assertEqual(self.coda.conta(DA_INVIARE), 0)

    def test_sito_in_errore_le_misure_restano(self):
        self.accoda(3)
        FintoSito.risposte = [(500, {"code": "dbmt_database"})]
        with self.assertLogs("meteo_bridge.invio", "WARNING"):
            self.inviatore.invia_coda(self.coda)
        self.assertEqual(self.coda.conta(DA_INVIARE), 3)

    def test_token_rifiutato_le_misure_restano(self):
        self.accoda(3)
        FintoSito.risposte = [(401, {"code": "dbmt_token"})]
        with self.assertLogs("meteo_bridge.invio", "ERROR"):
            self.inviatore.invia_coda(self.coda)
        self.assertEqual(self.coda.conta(DA_INVIARE), 3)

    def test_gruppo_non_valido_non_blocca_la_coda(self):
        self.accoda(3)
        FintoSito.risposte = [(400, {"code": "dbmt_corpo"})]
        with self.assertLogs("meteo_bridge.invio", "ERROR"):
            self.inviatore.invia_coda(self.coda)
        self.assertEqual(self.coda.conta(RIFIUTATA), 3)

    def test_sito_spento_le_misure_restano(self):
        self.accoda(3)
        spento = Inviatore("http://127.0.0.1:1/wp-json/dbmt/v1/misure", "segreto", ":memory:")
        with self.assertLogs("meteo_bridge.invio", "WARNING"):
            spento.invia_coda(self.coda)
        self.assertEqual(self.coda.conta(DA_INVIARE), 3)

    def test_coda_vuota_nessuna_richiesta(self):
        FintoSito.risposte = [(200, tutte_nuove)]
        self.inviatore.invia_coda(self.coda)
        self.assertEqual(FintoSito.ricevute, [])


class TestIndirizzo(unittest.TestCase):

    def test_indirizzi(self):
        self.assertTrue(indirizzo_sicuro("https://www.scuola.edu.it/wp-json/dbmt/v1/misure"))
        self.assertTrue(indirizzo_sicuro("http://lora-meteo-test.local/wp-json/dbmt/v1/misure"))
        self.assertTrue(indirizzo_sicuro("http://localhost:8080/wp-json/dbmt/v1/misure"))
        self.assertFalse(indirizzo_sicuro("http://www.scuola.edu.it/wp-json/dbmt/v1/misure"))
        self.assertFalse(indirizzo_sicuro("ftp://www.scuola.edu.it/"))


if __name__ == "__main__":
    unittest.main()
