/*
 * Ricevitore LoRa P2P - RAK3172 (RUI3 >= 4.1)
 * Verifica e decodifica i pacchetti dei nodi (formato v1: BME280 15 byte,
 * SHT45 13 byte) e li stampa in CSV su seriale per il bridge sul Raspberry.
 *
 * All'avvio: autodiagnosi con correzione automatica di modalità e parametri P2P
 * (devono essere IDENTICI a quelli dei nodi).
 *
 * Uscita CSV: id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr
 * Le misure non disponibili escono come campi vuoti; "reset" è valorizzato solo
 * nel primo pacchetto dopo un riavvio del nodo (seq 0). Tutte le altre righe
 * iniziano con '#' così il bridge le ignora.
 *
 * La callback di ricezione gira nell'interrupt della radio: lì il pacchetto viene
 * solo copiato in una coda, verifica e stampa avvengono in loop().
 */

// ---------- Configurazione (uguale ai nodi) ----------
#define P2P_FREQ  868100000UL
#define P2P_SF    7
#define P2P_BW    125
#define P2P_CR    0
#define P2P_PL    8
#define P2P_TXP   14
// -----------------------------------------------------

#define FW_VERSION    "1.0"

// Formato del pacchetto (uguale ai nodi)
#define FMT_VERSION   1
#define TYPE_BME280   1
#define TYPE_SHT45    2
#define LEN_BME280    15
#define LEN_SHT45     13

// Valori "non disponibile" nel payload (uguali ai nodi)
#define ND_I16    ((int16_t)0x8000)
#define ND_U16    0xFFFF

#define WDT_MS        8000
#define STATS_MS      600000UL      // statistiche e riavvio della ricezione ogni 10 min

// ---------- Coda fra interrupt e loop ----------
#define QUEUE_LEN     8             // potenza di 2
#define MAX_PKT       32

struct RxPacket { uint8_t len; int16_t rssi; int8_t snr; uint8_t buf[MAX_PKT]; };
RxPacket queue[QUEUE_LEN];
volatile uint8_t qHead = 0, qTail = 0;

// Contatori (aggiornati anche dall'interrupt)
volatile uint32_t nRadioErr = 0, nOverflow = 0, nTooLong = 0;
uint32_t nValid = 0, nCrc = 0, nFormat = 0;

uint8_t errors = 0, fixes = 0;

#define CHECK(name, cur, setter, want)                                                     \
  do {                                                                                     \
    unsigned long c_ = (unsigned long)(cur), w_ = (unsigned long)(want);                   \
    if (c_ == w_) { Serial.printf("#  %-10s %lu OK\r\n", name, w_); }                      \
    else if (setter) { Serial.printf("#  %-10s %lu -> %lu CORRETTO\r\n", name, c_, w_); fixes++; } \
    else { Serial.printf("#  %-10s ERRORE impostazione (%lu)\r\n", name, c_); errors++; }  \
  } while (0)

// ---------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ----------
uint16_t crc16(const uint8_t *p, uint8_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// ---------- Causa del reset ----------
const char *resetName(uint8_t c) {
  switch (c) {
    case 1:  return "alimentazione";
    case 2:  return "pin NRST";
    case 3:  return "software";
    case 4:  return "watchdog";
    case 5:  return "watchdog a finestra";
    case 6:  return "basso consumo";
    default: return "altro";
  }
}

// Va letta prima di abilitare il watchdog, che azzera i flag
uint8_t readResetCause() {
  uint32_t csr = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;
  if (csr & RCC_CSR_IWDGRSTF) return 4;
  if (csr & RCC_CSR_WWDGRSTF) return 5;
  if (csr & RCC_CSR_LPWRRSTF) return 6;
  if (csr & RCC_CSR_SFTRSTF)  return 3;
  if (csr & RCC_CSR_BORRSTF)  return 1;
  if (csr & RCC_CSR_PINRSTF)  return 2;
  return 7;
}

// ---------- Ricezione (interrupt) ----------
void recvCallback(rui_lora_p2p_recv_t data) {
  // Su errore CRC radio RUI3 richiama la callback con il buffer del pacchetto
  // precedente: va scartata, altrimenti ricompare un duplicato
  if (data.Status != LORA_P2P_RXDONE) { nRadioErr++; return; }
  if (data.BufferSize > MAX_PKT)      { nTooLong++;  return; }

  uint8_t next = (qHead + 1) & (QUEUE_LEN - 1);
  if (next == qTail) { nOverflow++; return; }
  RxPacket &p = queue[qHead];
  p.len = data.BufferSize;
  p.rssi = data.Rssi;
  p.snr = data.Snr;
  memcpy(p.buf, data.Buffer, data.BufferSize);
  qHead = next;
}

// ---------- Decodifica (loop) ----------
static inline uint16_t be16(const uint8_t *b) { return (uint16_t)(b[0] << 8 | b[1]); }

void handlePacket(const RxPacket &p) {
  const uint8_t *b = p.buf;
  uint8_t n = p.len;

  // Il CRC protegge anche dai pacchetti estranei con la stessa configurazione radio
  if (n < 5 || crc16(b, n - 2) != be16(b + n - 2)) {
    nCrc++;
    Serial.printf("# pacchetto scartato: CRC errato (%u byte, RSSI %d)\r\n", n, p.rssi);
    return;
  }

  uint8_t ver = b[0] >> 4, id = b[0] & 0x0F, seq = b[1];
  uint8_t type = b[2] >> 4, rst = b[2] & 0x0F;
  bool lenOk = (type == TYPE_BME280 && n == LEN_BME280) || (type == TYPE_SHT45 && n == LEN_SHT45);
  if (ver != FMT_VERSION || id == 0 || !lenOk) {
    nFormat++;
    Serial.printf("# pacchetto scartato: formato v%u, nodo %u, tipo %u, %u byte\r\n", ver, id, type, n);
    return;
  }
  nValid++;

  int16_t  t  = (int16_t)be16(b + 3);
  uint16_t rh = be16(b + 5);
  uint16_t p10 = ND_U16;
  const uint8_t *v = b + 7;          // tensioni: dopo T e UR per SHT45...
  if (type == TYPE_BME280) { p10 = be16(b + 7); v = b + 9; }   // ...dopo la pressione per BME280
  uint16_t vbat = be16(v), vtx = be16(v + 2);

  // Campi vuoti per i valori non disponibili
  char sT[10] = "", sRh[10] = "", sP[10] = "", sV[8] = "", sVtx[8] = "", sRst[4] = "";
  if (t    != ND_I16) snprintf(sT,   sizeof(sT),   "%.2f", t / 100.0f);
  if (rh   != ND_U16) snprintf(sRh,  sizeof(sRh),  "%.2f", rh / 100.0f);
  if (p10  != ND_U16) snprintf(sP,   sizeof(sP),   "%.1f", p10 / 10.0f);
  if (vbat != ND_U16) snprintf(sV,   sizeof(sV),   "%u", vbat);
  if (vtx  != ND_U16) snprintf(sVtx, sizeof(sVtx), "%u", vtx);
  if (seq == 0)       snprintf(sRst, sizeof(sRst), "%u", rst);

  if (seq == 0)
    Serial.printf("# nodo %u: riavvio (causa: %s)\r\n", id, resetName(rst));
  if (t == ND_I16 && rh == ND_U16)
    Serial.printf("# nodo %u: il sensore non risponde\r\n", id);
  Serial.printf("%u,%u,%s,%s,%s,%s,%s,%s,%s,%d,%d\r\n",
                id, seq, type == TYPE_BME280 ? "bme280" : "sht45", sRst,
                sT, sRh, sP, sV, sVtx, p.rssi, p.snr);
}

// ---------- Avvio ----------
void selfTest(uint8_t rst) {
  Serial.println("#");
  Serial.printf("# === Autodiagnosi ricevitore, firmware %s ===\r\n", FW_VERSION);
  Serial.printf("#  Causa reset: %s\r\n", resetName(rst));

  if (api.lorawan.nwm.get() != 0) {
    if (api.lora.nwm.set()) {
      Serial.println("#  Modalità LoRaWAN: passo a P2P e riavvio...");
      delay(200);
      api.system.reboot();
    }
    Serial.println("#  Modalità   ERRORE passaggio a P2P");
    errors++;
  } else {
    Serial.println("#  Modalità   P2P OK");
  }

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
  uint8_t rst = readResetCause();  // prima del watchdog
  Serial.begin(115200);
  delay(1500);                     // tempo per aprire il monitor seriale
  api.lora.registerPRecvCallback(recvCallback);
  selfTest(rst);                   // parametri corretti PRIMA di avviare la ricezione
  api.system.wdt.enable(WDT_MS);
  startRx();
  Serial.println("# id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr");
}

void loop() {
  api.system.wdt.reset();

  while (qTail != qHead) {
    handlePacket(queue[qTail]);
    qTail = (qTail + 1) & (QUEUE_LEN - 1);
  }

  // Ogni 10 minuti: statistiche e riavvio della ricezione per sicurezza
  static uint32_t last = 0;
  if (millis() - last >= STATS_MS) {
    last = millis();
    Serial.printf("# stat: %lu validi, %lu CRC errato, %lu errori radio, %lu formato, %lu coda piena, %lu troppo lunghi\r\n",
                  nValid, nCrc, (unsigned long)nRadioErr, nFormat, (unsigned long)nOverflow, (unsigned long)nTooLong);
    api.lora.precv(0);             // ferma
    delay(50);
    startRx();                     // riparte
  }
  delay(10);
}
