/*
 * Ricevitore LoRa P2P - RAK3172 (RUI3 >= 4.1)
 * Decodifica i pacchetti dei nodi SHT45 (8 byte) e BME280 (10 byte)
 * e li stampa in CSV su seriale per il bridge Python sul Raspberry.
 *
 * All'avvio: autodiagnosi con correzione automatica di modalità e parametri P2P
 * (devono essere IDENTICI a quelli dei nodi).
 *
 * Uscita CSV: id,seq,temp_C,ur_pct,press_hPa,vbat_mV,rssi,snr
 * I valori "non disponibile" (T = 0x8000, UR o P = 0xFFFF: sensore che non
 * risponde) escono come campi vuoti. Tutte le altre righe iniziano con '#'
 * così il bridge le ignora.
 */

// ---------- Configurazione (uguale ai nodi) ----------
#define P2P_FREQ  868100000UL
#define P2P_SF    7
#define P2P_BW    125
#define P2P_CR    0
#define P2P_PL    8
#define P2P_TXP   14
// -----------------------------------------------------

// Valori "non disponibile" nel payload (uguali ai nodi)
#define ND_I16    ((int16_t)0x8000)
#define ND_U16    0xFFFF

uint8_t errors = 0, fixes = 0;
uint32_t rxCount = 0, rxBad = 0;

#define CHECK(name, cur, setter, want)                                                     \
  do {                                                                                     \
    unsigned long c_ = (unsigned long)(cur), w_ = (unsigned long)(want);                   \
    if (c_ == w_) { Serial.printf("#  %-10s %lu OK\r\n", name, w_); }                      \
    else if (setter) { Serial.printf("#  %-10s %lu -> %lu CORRETTO\r\n", name, c_, w_); fixes++; } \
    else { Serial.printf("#  %-10s ERRORE impostazione (%lu)\r\n", name, c_); errors++; }  \
  } while (0)

void recvCallback(rui_lora_p2p_recv_t data) {
  uint8_t *b = data.Buffer;
  uint8_t n = data.BufferSize;

  if (n != 8 && n != 10) {
    rxBad++;
    Serial.printf("# pacchetto ignorato (%u byte, RSSI %d)\r\n", n, data.Rssi);
    return;
  }
  rxCount++;

  uint8_t  id  = b[0];
  uint8_t  seq = b[1];
  int16_t  t   = (int16_t)((b[2] << 8) | b[3]);
  uint16_t rh  = (b[4] << 8) | b[5];
  uint16_t p   = ND_U16;                         // SHT45: niente pressione
  uint16_t vbat;
  if (n == 8) {                                  // SHT45
    vbat = (b[6] << 8) | b[7];
  } else {                                       // BME280
    p    = (b[6] << 8) | b[7];
    vbat = (b[8] << 8) | b[9];
  }

  // Campi vuoti per i valori non disponibili
  char sT[10] = "", sRh[10] = "", sP[10] = "";
  if (t  != ND_I16) snprintf(sT,  sizeof(sT),  "%.2f", t / 100.0f);
  if (rh != ND_U16) snprintf(sRh, sizeof(sRh), "%.2f", rh / 100.0f);
  if (p  != ND_U16) snprintf(sP,  sizeof(sP),  "%.1f", p / 10.0f);

  if (t == ND_I16 && rh == ND_U16)
    Serial.printf("# nodo %u: il sensore non risponde\r\n", id);
  Serial.printf("%u,%u,%s,%s,%s,%u,%d,%d\r\n",
                id, seq, sT, sRh, sP, vbat, data.Rssi, data.Snr);
}

void selfTest() {
  Serial.println("#");
  Serial.println("# === Autodiagnosi ricevitore ===");

  if (api.lorawan.nwm.get() != 0) {
    Serial.println("#  Modalità LoRaWAN: passo a P2P e riavvio...");
    api.lora.nwm.set();
    delay(200);
    api.system.reboot();
  }
  Serial.println("#  Modalità   P2P OK");

  CHECK("Frequenza", api.lora.pfreq.get(), api.lora.pfreq.set(P2P_FREQ), P2P_FREQ);
  CHECK("SF",        api.lora.psf.get(),   api.lora.psf.set(P2P_SF),     P2P_SF);
  CHECK("Banda",     api.lora.pbw.get(),   api.lora.pbw.set(P2P_BW),     P2P_BW);
  CHECK("CR",        api.lora.pcr.get(),   api.lora.pcr.set(P2P_CR),     P2P_CR);
  CHECK("Preambolo", api.lora.ppl.get(),   api.lora.ppl.set(P2P_PL),     P2P_PL);
  CHECK("Potenza",   api.lora.ptp.get(),   api.lora.ptp.set(P2P_TXP),    P2P_TXP);

  Serial.printf("# === Esito: %u errori, %u correzioni ===\r\n", errors, fixes);
}

void startRx() {
  // 65534 = ricezione continua (non si ferma dopo il primo pacchetto)
  if (api.lora.precv(65534)) {
    Serial.println("# In ascolto...");
  } else {
    Serial.println("# ERRORE avvio ricezione");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);                     // tempo per aprire il monitor seriale
  api.lora.registerPRecvCallback(recvCallback);
  selfTest();                      // parametri corretti PRIMA di avviare la ricezione
  startRx();
  Serial.println("# id,seq,temp_C,ur_pct,press_hPa,vbat_mV,rssi,snr");
}

void loop() {
  // Ogni 10 minuti: statistiche e riavvio della ricezione per sicurezza
  static uint32_t last = 0;
  if (millis() - last >= 600000UL) {
    last = millis();
    Serial.printf("# stat: %lu pacchetti validi, %lu scartati\r\n", rxCount, rxBad);
    api.lora.precv(0);             // ferma
    delay(50);
    startRx();                     // riparte
  }
  delay(100);
}
