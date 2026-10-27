/*
 * Nodo sensore T/UR/pressione - RAK3172 (RUI3 >= 4.1) + BME280 - LoRa P2P
 * Autore: Davide Bertolino
 * Firmware 1.2 (aggiornare insieme a FW_VERSION)
 *
 * Con autodiagnosi all'avvio: controlla e corregge modalità radio e parametri P2P,
 * verifica sensore e alimentazione.
 *
 * Alimentazione: 2 stilo AA alcaline collegate direttamente al pin 3V3 del modulo
 * (2,0-3,6 V), senza regolatore: la tensione di alimentazione del modulo è quella
 * delle batterie, e il nodo la misura con il riferimento interno (readVbat).
 *
 * Collegamenti: VCC->3V3, GND->GND, SDA->PA11, SCL->PA12
 * (moduli a 6 pin: CSB a 3V3 per I2C, SDO a GND = 0x76, a 3V3 = 0x77)
 *
 * Pacchetto, formato v1 (15 byte, big endian):
 *   [0]     versione formato (4 bit alti) | ID nodo (4 bit bassi, 1-15)
 *   [1]     contatore: 0 = primo pacchetto dopo un riavvio, poi 1…255, 1…
 *   [2]     tipo nodo (4 bit alti, 1 = BME280) | causa del reset (4 bit bassi,
 *           solo nel pacchetto con contatore 0, altrimenti 0)
 *   [3-4]   T °C x100 (int16)        [5-6]  UR % x100
 *   [7-8]   P hPa x10                [9-10] Vbat a riposo, mV
 *   [11-12] Vbat durante la trasmissione PRECEDENTE, mV
 *   [13-14] CRC-16/CCITT-FALSE dei byte 0-12
 * Misure non disponibili (sensore che non risponde, Vbat in trasmissione non
 * misurata): T = 0x8000, gli altri campi 0xFFFF.
 */

#include <Wire.h>

// ---------- Configurazione ----------
#define NODE_ID      2             // 1-15
#define INTERVAL_MS  30000UL       // 30 s test; in esercizio 600000UL (10 min)
#define NODE_DEBUG   1             // 0 = nessuna stampa (installazione definitiva)

#define P2P_FREQ     868100000UL   // 868,1 MHz: con BW 125 kHz resta tutta in 868,0-868,6 (duty cycle 1 %)
#define P2P_SF       7
#define P2P_BW       125
#define P2P_CR       0             // 0 = 4/5
#define P2P_PL       8
#define P2P_TXP      14

#define VBAT_MIN_MV  2300          // 2 alcaline a ~1,15 V: quasi scariche (il modulo funziona fino a 2000)
#define VBAT_MAX_MV  3600          // massimo di alimentazione del RAK3172 (2,0-3,6 V)
// ------------------------------------

#define FW_VERSION   "1.2"

// Formato del pacchetto
#define FMT_VERSION  1
#define NODE_TYPE    1             // 1 = BME280, 2 = SHT45
#define PKT_LEN      15

// Valori "non disponibile" nel payload: fuori da qualsiasi misura reale
#define ND_I16       ((int16_t)0x8000)
#define ND_U16       0xFFFF

// Cause del reset (byte [2], 4 bit bassi)
#define RST_NESSUNO     0          // pacchetto normale
#define RST_ALIM        1          // accensione o calo di tensione (BOR)
#define RST_PIN         2          // pin NRST
#define RST_SOFTWARE    3          // riavvio da firmware (es. passaggio a P2P)
#define RST_WATCHDOG    4          // watchdog indipendente (IWDG)
#define RST_WWDG        5          // watchdog a finestra
#define RST_LOWPOWER    6          // errore di gestione del basso consumo
#define RST_ALTRO       7          // option byte o causa non riconosciuta

// Watchdog: massimo 8 s in RUI3; continua a contare anche in sleep, quindi
// lo sleep lungo è spezzato in tratti più brevi, con una ricarica fra l'uno e l'altro
#define WDT_MS       8000
#define SLEEP_STEP_MS 5000UL

// Ritardo casuale sull'intervallo (±10 %): evita che due nodi accesi insieme
// trasmettano sempre nello stesso istante e collidano a ogni ciclo
#define JITTER_MS    (INTERVAL_MS / 10)

// Pacchetto da ~46 ms in aria: con l'1 % di duty cycle servirebbero almeno 4,6 s
// fra due trasmissioni. 10 s lascia margine anche con il ritardo casuale.
static_assert(INTERVAL_MS >= 10000UL, "INTERVAL_MS troppo breve per il duty cycle");
static_assert(NODE_ID >= 1 && NODE_ID <= 15, "NODE_ID deve stare fra 1 e 15");

#if NODE_DEBUG
  #define DBG(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(...) do {} while (0)
#endif

uint8_t addr = 0;
uint8_t seq = 0;
uint8_t resetCause = RST_ALTRO;
uint16_t vbatTx = ND_U16;            // misurata durante la trasmissione precedente
volatile bool txDone = false;

uint16_t T1, P1; int16_t T2, T3, P2, P3, P4, P5, P6, P7, P8, P9;
uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
int32_t t_fine;

void sendDone(void) { txDone = true; }

// Tensione di alimentazione = tensione delle batterie (niente regolatore), in mV.
// Si misura il riferimento interno VREFINT dell'STM32WL e lo si confronta con
// la sua taratura di fabbrica (VREFINT_CAL, acquisita a 3,3 V): più la tensione
// è bassa, più VREFINT occupa la scala dell'ADC. È il metodo che RUI3 usa sul
// RAK3172-SiP; sul RAK3172 api.system.bat.get() di RUI3 4.3.0 non legge l'ADC
// (service_battery.c) e restituisce sempre 3,0 V.
uint16_t readVbat() {
  UDRV_ADC_RESOLUTION prima = udrv_adc_get_resolution();
  udrv_adc_set_resolution(UDRV_ADC_RESOLUTION_12BIT);
  uint32_t somma = 0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < 5; i++) {
    int16_t v = 0;                   // udrv_adc_read vuole int16_t; a 12 bit il valore sta fra 0 e 4095
    if (udrv_adc_read(UDRV_ADC_CHANNEL_VREFINT, &v) != UDRV_RETURN_OK || v <= 0) continue;
    if (i == 0) continue;            // la prima conversione dopo l'accensione del riferimento è meno precisa
    somma += (uint32_t)v;
    n++;
  }
  udrv_adc_set_resolution(prima);
  if (n == 0) return ND_U16;
  return (uint16_t)__LL_ADC_CALC_VREFANALOG_VOLTAGE(somma / n, LL_ADC_RESOLUTION_12B);
}

// ---------- Causa del reset ----------
// Va letta prima di abilitare il watchdog: RUI3 azzera i flag nell'abilitarlo.
// Il flag BOR è attivo sia all'accensione sia dopo un calo di tensione: i due casi
// si distinguono solo dalla tensione nello stesso pacchetto.
uint8_t readResetCause() {
  uint32_t csr = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;          // i flag si accumulano fino a che non si azzerano
  // Ogni reset interno attiva anche PINRSTF: l'ordine dei controlli conta
  if (csr & RCC_CSR_IWDGRSTF) return RST_WATCHDOG;
  if (csr & RCC_CSR_WWDGRSTF) return RST_WWDG;
  if (csr & RCC_CSR_LPWRRSTF) return RST_LOWPOWER;
  if (csr & RCC_CSR_SFTRSTF)  return RST_SOFTWARE;
  if (csr & RCC_CSR_BORRSTF)  return RST_ALIM;
  if (csr & RCC_CSR_PINRSTF)  return RST_PIN;
  return RST_ALTRO;
}

const char *resetName(uint8_t c) {
  switch (c) {
    case RST_ALIM:     return "alimentazione";
    case RST_PIN:      return "pin NRST";
    case RST_SOFTWARE: return "software";
    case RST_WATCHDOG: return "watchdog";
    case RST_WWDG:     return "watchdog a finestra";
    case RST_LOWPOWER: return "basso consumo";
    default:           return "altro";
  }
}

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

// Pacchetto formato v1 (vedi intestazione); la causa del reset va solo con seq 0
void buildPacket(uint8_t *pl, uint8_t sq, uint8_t rst, int16_t t, uint16_t rh,
                 uint16_t p, uint16_t vbat, uint16_t vtx) {
  uint8_t i = 0;
  pl[i++] = FMT_VERSION << 4 | NODE_ID;
  pl[i++] = sq;
  pl[i++] = NODE_TYPE << 4 | (sq == 0 ? rst : RST_NESSUNO);
  pl[i++] = (uint16_t)t >> 8;  pl[i++] = (uint8_t)t;
  pl[i++] = rh >> 8;           pl[i++] = (uint8_t)rh;
  pl[i++] = p >> 8;            pl[i++] = (uint8_t)p;
  pl[i++] = vbat >> 8;         pl[i++] = (uint8_t)vbat;
  pl[i++] = vtx >> 8;          pl[i++] = (uint8_t)vtx;
  uint16_t crc = crc16(pl, i);
  pl[i++] = crc >> 8;          pl[i++] = (uint8_t)crc;
}

// ---------- Bus I2C ----------
// Se il sensore si è fermato a metà di una trasmissione può tenere SDA basso e
// bloccare il bus: 9 impulsi di clock gli fanno finire il byte, poi una
// condizione di STOP lo riporta a riposo.
void i2cRecover() {
  Wire.end();
  pinMode(PA11, INPUT_PULLUP);       // SDA
  pinMode(PA12, OUTPUT);             // SCL
  digitalWrite(PA12, HIGH);
  delayMicroseconds(5);
  for (uint8_t i = 0; i < 9 && digitalRead(PA11) == LOW; i++) {
    digitalWrite(PA12, LOW);  delayMicroseconds(5);
    digitalWrite(PA12, HIGH); delayMicroseconds(5);
  }
  pinMode(PA11, OUTPUT);             // STOP: SDA da basso ad alto con SCL alto
  digitalWrite(PA11, LOW);  delayMicroseconds(5);
  digitalWrite(PA12, HIGH); delayMicroseconds(5);
  digitalWrite(PA11, HIGH); delayMicroseconds(5);
  Wire.begin();
}

// ---------- BME280 ----------
void wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

bool rd(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(addr); Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(addr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool bmeInit() {
  uint8_t id = 0;
  const uint8_t addrs[] = {0x76, 0x77};
  for (uint8_t i = 0; i < 2; i++) {
    addr = addrs[i];
    if (rd(0xD0, &id, 1)) break;
    addr = 0;
  }
  if (!addr)       { DBG("  BME280: non trovato su 0x76/0x77 (collegamenti? pull-up?)\r\n"); return false; }
  if (id == 0x58)  { DBG("  BME280: è un BMP280 (niente umidità)\r\n"); addr = 0; return false; }
  if (id != 0x60)  { DBG("  BME280: chip ID inatteso 0x%02X\r\n", id); addr = 0; return false; }

  wr(0xE0, 0xB6); delay(5);

  uint8_t c[26], h[7];
  if (!rd(0x88, c, 26) || !rd(0xE1, h, 7)) {
    DBG("  BME280: errore lettura calibrazione\r\n"); addr = 0; return false;
  }
  T1 = c[1] << 8 | c[0];  T2 = c[3] << 8 | c[2];  T3 = c[5] << 8 | c[4];
  P1 = c[7] << 8 | c[6];  P2 = c[9] << 8 | c[8];  P3 = c[11] << 8 | c[10];
  P4 = c[13] << 8 | c[12]; P5 = c[15] << 8 | c[14]; P6 = c[17] << 8 | c[16];
  P7 = c[19] << 8 | c[18]; P8 = c[21] << 8 | c[20]; P9 = c[23] << 8 | c[22];
  H1 = c[25];
  H2 = h[1] << 8 | h[0];  H3 = h[2];
  H4 = (int16_t)((int8_t)h[3] * 16) | (h[4] & 0x0F);
  H5 = (int16_t)((int8_t)h[5] * 16) | (h[4] >> 4);
  H6 = (int8_t)h[6];

  wr(0xF5, 0x00);
  DBG("  BME280: OK a 0x%02X\r\n", addr);
  return true;
}

int32_t compT(int32_t adc) {
  int32_t v1 = ((((adc >> 3) - ((int32_t)T1 << 1))) * T2) >> 11;
  int32_t v2 = (((((adc >> 4) - (int32_t)T1) * ((adc >> 4) - (int32_t)T1)) >> 12) * T3) >> 14;
  t_fine = v1 + v2;
  return (t_fine * 5 + 128) >> 8;
}

uint32_t compP(int32_t adc) {
  int64_t v1 = (int64_t)t_fine - 128000;
  int64_t v2 = v1 * v1 * P6;
  v2 += (v1 * P5) << 17;
  v2 += ((int64_t)P4) << 35;
  v1 = ((v1 * v1 * P3) >> 8) + ((v1 * P2) << 12);
  v1 = ((((int64_t)1) << 47) + v1) * P1 >> 33;
  if (v1 == 0) return 0;
  int64_t p = 1048576 - adc;
  p = (((p << 31) - v2) * 3125) / v1;
  v1 = ((int64_t)P9 * (p >> 13) * (p >> 13)) >> 25;
  v2 = ((int64_t)P8 * p) >> 19;
  return (uint32_t)(((p + v1 + v2) >> 8) + ((int64_t)P7 << 4));
}

uint32_t compH(int32_t adc) {
  int32_t v = t_fine - 76800;
  v = (((((adc << 14) - ((int32_t)H4 << 20) - ((int32_t)H5 * v)) + 16384) >> 15) *
       (((((((v * (int32_t)H6) >> 10) * (((v * (int32_t)H3) >> 11) + 32768)) >> 10) + 2097152) *
         (int32_t)H2 + 8192) >> 14));
  v -= ((((v >> 15) * (v >> 15)) >> 7) * (int32_t)H1) >> 4;
  if (v < 0) v = 0;
  if (v > 419430400) v = 419430400;
  return (uint32_t)(v >> 12);
}

bool bmeRead(int16_t &t100, uint16_t &rh100, uint16_t &p10) {
  wr(0xF2, 0x01);
  wr(0xF4, 0x25);
  uint8_t st = 0; uint32_t t0 = millis();
  do {
    delay(2);
    if (!rd(0xF3, &st, 1)) return false;
  } while ((st & 0x08) && millis() - t0 < 100);
  if (st & 0x08) return false;               // misura non conclusa: dati vecchi

  uint8_t d[8];
  if (!rd(0xF7, d, 8)) return false;
  int32_t adcP = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
  int32_t adcT = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
  int32_t adcH = (d[6] << 8) | d[7];
  // Valori di reset dei registri: il sensore si è riavviato e non ha misurato
  if (adcT == 0x80000 || adcP == 0x80000 || adcH == 0x8000) return false;

  int32_t  t = compT(adcT);
  uint32_t p = compP(adcP) / 2560;
  uint32_t h = (compH(adcH) * 100) >> 10;
  // Fuori dal campo di misura del BME280: lettura corrotta, non una misura
  if (t < -4000 || t > 8500 || p < 3000 || p > 11000 || h > 10000) return false;

  t100 = (int16_t)t; p10 = (uint16_t)p; rh100 = (uint16_t)h;
  return true;
}

// Se la lettura fallisce il bus I2C viene sbloccato, il sensore reinizializzato
// (indirizzo, chip ID, calibrazione) e riletto subito, così un errore transitorio
// non costa un ciclo. Se non risponde ancora, ci si riprova al ciclo successivo.
bool sensorRead(int16_t &t100, uint16_t &rh100, uint16_t &p10) {
  if (addr && bmeRead(t100, rh100, p10)) return true;
  if (addr) DBG("BME280: lettura fallita, reinizializzo\r\n");
  addr = 0;
  i2cRecover();
  return bmeInit() && bmeRead(t100, rh100, p10);
}

// ---------- Autodiagnosi ----------
uint8_t errors = 0, fixes = 0;

#define CHECK(name, cur, setter, want)                                            \
  do {                                                                            \
    unsigned long c_ = (unsigned long)(cur), w_ = (unsigned long)(want);          \
    if (c_ == w_) { DBG("  %-10s %lu OK\r\n", name, w_); }                        \
    else if (setter) { DBG("  %-10s %lu -> %lu CORRETTO\r\n", name, c_, w_); fixes++; } \
    else { DBG("  %-10s ERRORE impostazione (%lu)\r\n", name, c_); errors++; }     \
  } while (0)

// pbw.get() di RUI3 4.3.0 restituisce l'indice della banda (0 = 125 kHz,
// 1 = 250, 2 = 500), pbw.set() accetta i kHz: si confronta in kHz
uint32_t bwKhz(uint32_t v) {
  switch (v) {
    case 0:  return 125;
    case 1:  return 250;
    case 2:  return 500;
    default: return v;
  }
}

void selfTest() {
  DBG("\r\n=== Autodiagnosi nodo %u, firmware %s ===\r\n", NODE_ID, FW_VERSION);
  DBG("[Avvio]\r\n  Causa reset: %s\r\n", resetName(resetCause));

  // 1. Modalità radio
  DBG("[Radio]\r\n");
  if (api.lorawan.nwm.get() != 0) {
    if (api.lora.nwm.set()) {
      DBG("  Modalità LoRaWAN: passo a P2P e riavvio...\r\n");
      delay(200);
      api.system.reboot();
    }
    // Senza P2P non si riavvia in continuazione: segnala e prosegue
    DBG("  Modalità   ERRORE passaggio a P2P\r\n"); errors++;
  } else {
    DBG("  Modalità   P2P OK\r\n");
  }

  // 2. Parametri P2P (salvati in flash, corretti solo se diversi)
  CHECK("Frequenza", api.lora.pfreq.get(), api.lora.pfreq.set(P2P_FREQ), P2P_FREQ);
  CHECK("SF",        api.lora.psf.get(),   api.lora.psf.set(P2P_SF),     P2P_SF);
  CHECK("Banda",     bwKhz(api.lora.pbw.get()), api.lora.pbw.set(P2P_BW), P2P_BW);
  CHECK("CR",        api.lora.pcr.get(),   api.lora.pcr.set(P2P_CR),     P2P_CR);
  CHECK("Preambolo", api.lora.ppl.get(),   api.lora.ppl.set(P2P_PL),     P2P_PL);
  CHECK("Potenza",   api.lora.ptp.get(),   api.lora.ptp.set(P2P_TXP),    P2P_TXP);

  // 3. Sensore
  DBG("[Sensore]\r\n");
  if (!bmeInit()) errors++;

  // 4. Alimentazione
  DBG("[Alimentazione]\r\n");
  uint16_t vbat = readVbat();
  if (vbat == ND_U16)          { DBG("  Vbat ERRORE lettura ADC\r\n"); errors++; }
  else if (vbat < VBAT_MIN_MV) { DBG("  Vbat %u mV BASSA\r\n", vbat); errors++; }
  else if (vbat > VBAT_MAX_MV) { DBG("  Vbat %u mV ALTA (max modulo %u mV)\r\n", vbat, VBAT_MAX_MV); errors++; }
  else                           DBG("  Vbat %u mV OK\r\n", vbat);

  DBG("=== Esito: %u errori, %u correzioni. Intervallo %lu s ±%lu s ===\r\n\r\n",
      errors, fixes, INTERVAL_MS / 1000, JITTER_MS / 1000);
}

// ---------- Sleep con watchdog ----------
// Il tempo si conta con millis(), che in RUI3 viene dall'RTC e avanza anche in
// sleep: se uno sleep termina in anticipo (es. la radio tiene un wake lock) il
// nodo si riaddormenta invece di accorciare l'intervallo e trasmettere prima.
void sleepWithWatchdog(uint32_t ms) {
  uint32_t start = millis();
  uint32_t elapsed;
  while ((elapsed = millis() - start) < ms) {
    uint32_t left = ms - elapsed;
    api.system.wdt.reset();
    api.system.sleep.all(left > SLEEP_STEP_MS ? SLEEP_STEP_MS : left);
  }
  api.system.wdt.reset();
}

// Seme del generatore casuale diverso per ogni modulo (DevEUI) e per ogni avvio
void seedRandom() {
  uint8_t eui[8] = {0};
  api.lorawan.deui.get(eui, 8);
  uint32_t s = NODE_ID;
  for (uint8_t i = 0; i < 8; i++) s = s * 31 + eui[i];
  randomSeed(s ^ readVbat() ^ millis());
}

// ---------- Main ----------
void setup() {
  resetCause = readResetCause();     // prima di tutto, prima del watchdog
  Serial.begin(115200);
  delay(1500);                       // tempo per aprire il monitor seriale
  Wire.begin();
  api.lora.registerPSendCallback(sendDone);
  selfTest();
  seedRandom();
  api.system.wdt.enable(WDT_MS);     // da qui il nodo si riavvia se resta bloccato più di 8 s
}

void loop() {
  api.system.wdt.reset();

  int16_t t; uint16_t rh, p;
  bool ok = sensorRead(t, rh, p);
  if (!ok) { t = ND_I16; rh = ND_U16; p = ND_U16; }
  uint16_t vbat = readVbat();

  uint8_t pl[PKT_LEN];
  buildPacket(pl, seq, resetCause, t, rh, p, vbat, vbatTx);

  if (ok) DBG("T=%.2f C  UR=%.2f %%  P=%.1f hPa  ", t / 100.0f, rh / 100.0f, p / 10.0f);
  else    DBG("BME280 NON RISPONDE  ");
  DBG("Vbat=%u mV  Vtx=%u mV  seq=%u  ", vbat, vbatTx, seq);
  if (seq == 0) DBG("(avvio: %s)  ", resetName(resetCause));

  // Il contatore avanza anche se l'invio fallisce: il salto segnala il pacchetto mancato
  seq = (seq == 255) ? 1 : seq + 1;

  txDone = false;
  vbatTx = ND_U16;
  if (api.lora.psend(sizeof(pl), pl)) {
    // Tensione sotto carico: a metà dei ~46 ms di trasmissione.
    // Va nel pacchetto successivo, questo è già partito.
    delay(20);
    if (!txDone) vbatTx = readVbat();
    uint32_t t0 = millis();
    while (!txDone && millis() - t0 < 3000) delay(10);
    DBG(txDone ? "TX ok\r\n" : "TX timeout\r\n");
  } else {
    DBG("ERRORE psend\r\n");
  }

  // Intervallo ± ritardo casuale
  uint32_t wait = INTERVAL_MS - JITTER_MS + (uint32_t)random(2 * JITTER_MS + 1);
  sleepWithWatchdog(wait);
}
