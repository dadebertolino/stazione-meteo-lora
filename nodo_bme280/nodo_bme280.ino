/*
 * Nodo sensore T/UR/pressione - RAK3172 (RUI3 >= 4.1) + BME280 - LoRa P2P
 * Con autodiagnosi all'avvio: controlla e corregge modalità radio e parametri P2P,
 * verifica sensore e alimentazione.
 *
 * Collegamenti: VCC->3V3, GND->GND, SDA->PA11, SCL->PA12
 * (moduli a 6 pin: CSB a 3V3 per I2C, SDO a GND = 0x76, a 3V3 = 0x77)
 *
 * Payload (10 byte, big endian):
 *   [0] ID  [1] contatore  [2-3] T x100 (int16)  [4-5] UR x100  [6-7] P hPa x10  [8-9] Vbat mV
 * Se il sensore non risponde il pacchetto parte lo stesso, con T = 0x8000 e
 * UR = P = 0xFFFF ("non disponibile"): il ricevitore distingue così un sensore
 * guasto da un nodo spento.
 */

#include <Wire.h>

// ---------- Configurazione ----------
#define NODE_ID      2
#define INTERVAL_MS  30000UL       // 30 s test; in esercizio 600000UL (10 min)
#define DEBUG        1             // 0 = nessuna stampa (installazione definitiva)

#define P2P_FREQ     868100000UL   // 868,1 MHz: con BW 125 kHz resta tutta in 868,0-868,6 (duty cycle 1 %)
#define P2P_SF       7
#define P2P_BW       125
#define P2P_CR       0             // 0 = 4/5
#define P2P_PL       8
#define P2P_TXP      14

#define VBAT_MIN_MV  2800
#define VBAT_MAX_MV  3700
// ------------------------------------

// Valori "non disponibile" nel payload: fuori da qualsiasi misura reale
#define ND_I16       ((int16_t)0x8000)
#define ND_U16       0xFFFF

#if DEBUG
  #define DBG(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(...) do {} while (0)
#endif

uint8_t addr = 0;
uint8_t seq = 0;
volatile bool txDone = false;

uint16_t T1, P1; int16_t T2, T3, P2, P3, P4, P5, P6, P7, P8, P9;
uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
int32_t t_fine;

void sendDone(void) { txDone = true; }

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

  t100  = (int16_t)compT(adcT);
  p10   = (uint16_t)(compP(adcP) / 2560);
  rh100 = (uint16_t)((compH(adcH) * 100) >> 10);
  return true;
}

// Se la lettura fallisce il sensore viene reinizializzato (indirizzo, chip ID,
// calibrazione) e riletto subito, così un errore transitorio non costa un ciclo.
// Se non risponde ancora, ci si riprova al ciclo successivo.
bool sensorRead(int16_t &t100, uint16_t &rh100, uint16_t &p10) {
  if (addr && bmeRead(t100, rh100, p10)) return true;
  if (addr) DBG("BME280: lettura fallita, reinizializzo\r\n");
  addr = 0;
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

void selfTest() {
  DBG("\r\n=== Autodiagnosi nodo %u ===\r\n", NODE_ID);

  // 1. Modalità radio
  DBG("[Radio]\r\n");
  if (api.lorawan.nwm.get() != 0) {
    DBG("  Modalità LoRaWAN: passo a P2P e riavvio...\r\n");
    api.lora.nwm.set();
    delay(200);
    api.system.reboot();
  }
  DBG("  Modalità   P2P OK\r\n");

  // 2. Parametri P2P (salvati in flash, corretti solo se diversi)
  CHECK("Frequenza", api.lora.pfreq.get(), api.lora.pfreq.set(P2P_FREQ), P2P_FREQ);
  CHECK("SF",        api.lora.psf.get(),   api.lora.psf.set(P2P_SF),     P2P_SF);
  CHECK("Banda",     api.lora.pbw.get(),   api.lora.pbw.set(P2P_BW),     P2P_BW);
  CHECK("CR",        api.lora.pcr.get(),   api.lora.pcr.set(P2P_CR),     P2P_CR);
  CHECK("Preambolo", api.lora.ppl.get(),   api.lora.ppl.set(P2P_PL),     P2P_PL);
  CHECK("Potenza",   api.lora.ptp.get(),   api.lora.ptp.set(P2P_TXP),    P2P_TXP);

  // 3. Sensore
  DBG("[Sensore]\r\n");
  if (!bmeInit()) errors++;

  // 4. Alimentazione
  DBG("[Alimentazione]\r\n");
  uint16_t vbat = (uint16_t)(api.system.bat.get() * 1000);
  if (vbat < VBAT_MIN_MV)      { DBG("  Vbat %u mV BASSA\r\n", vbat); errors++; }
  else if (vbat > VBAT_MAX_MV) { DBG("  Vbat %u mV ALTA (max modulo 3600 mV)\r\n", vbat); errors++; }
  else                           DBG("  Vbat %u mV OK\r\n", vbat);

  DBG("=== Esito: %u errori, %u correzioni. Intervallo %lu s ===\r\n\r\n",
      errors, fixes, INTERVAL_MS / 1000);
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(1500);                       // tempo per aprire il monitor seriale
  Wire.begin();
  api.lora.registerPSendCallback(sendDone);
  selfTest();
}

void loop() {
  int16_t t; uint16_t rh, p;
  bool ok = sensorRead(t, rh, p);
  if (!ok) { t = ND_I16; rh = ND_U16; p = ND_U16; }

  uint16_t vbat = (uint16_t)(api.system.bat.get() * 1000);
  uint8_t pl[10] = {
    NODE_ID, seq++,
    (uint8_t)(t >> 8), (uint8_t)t,
    (uint8_t)(rh >> 8), (uint8_t)rh,
    (uint8_t)(p >> 8), (uint8_t)p,
    (uint8_t)(vbat >> 8), (uint8_t)vbat
  };
  if (ok) DBG("T=%.2f C  UR=%.2f %%  P=%.1f hPa  ", t / 100.0f, rh / 100.0f, p / 10.0f);
  else    DBG("BME280 NON RISPONDE  ");
  DBG("Vbat=%u mV  seq=%u  ", vbat, pl[1]);

  txDone = false;
  if (api.lora.psend(sizeof(pl), pl)) {
    uint32_t t0 = millis();
    while (!txDone && millis() - t0 < 3000) delay(10);
    DBG(txDone ? "TX ok\r\n" : "TX timeout\r\n");
  } else {
    DBG("ERRORE psend\r\n");
  }
  api.system.sleep.all(INTERVAL_MS);
}
