#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
// ---- stub minimi al posto di RUI3 ----
std::string out;
struct SerialStub {
  int printf(const char *f, ...) { char b[512]; va_list a; va_start(a, f); int n = vsnprintf(b, sizeof b, f, a); va_end(a); out += b; return n; }
  void println(const char *s) { out += s; out += "\r\n"; }
} Serial;
struct { uint32_t CSR; } RCCs; 
#define RCC (&RCCs)
#define RCC_CSR_RMVF (1u<<23)
#define RCC_CSR_IWDGRSTF (1u<<29)
#define RCC_CSR_WWDGRSTF (1u<<30)
#define RCC_CSR_LPWRRSTF (1u<<31)
#define RCC_CSR_SFTRSTF (1u<<28)
#define RCC_CSR_BORRSTF (1u<<27)
#define RCC_CSR_PINRSTF (1u<<26)
enum { LORA_P2P_RXDONE = 0, LORA_P2P_RXTIMEOUT, LORA_P2P_RXERROR };
struct rui_lora_p2p_recv_t { uint8_t *Buffer; uint16_t BufferSize; int16_t Rssi; int8_t Snr; int Status; };
#include "estratto.inc"
// ---- test ----
int fails = 0;
#define EXPECT(cond, msg) do { if (cond) printf("ok    %s\n", msg); else { printf("FALLITO %s\n", msg); fails++; } } while (0)
std::string run(const uint8_t *b, uint8_t n, int status = LORA_P2P_RXDONE) {
  out.clear();
  rui_lora_p2p_recv_t d{(uint8_t *)b, n, -72, 9, status};
  rx::recvCallback(d);
  while (rx::qTail != rx::qHead) { rx::handlePacket(rx::queue[rx::qTail]); rx::qTail = (rx::qTail + 1) & (QUEUE_LEN - 1); }
  return out;
}
bool has(const std::string &s, const char *x) { return s.find(x) != std::string::npos; }
int main() {
  const uint8_t v[] = "123456789";
  EXPECT(nodo::crc16(v, 9) == 0x29B1, "CRC nodo: vettore standard 0x29B1");
  EXPECT(rx::crc16(v, 9) == 0x29B1, "CRC ricevitore: vettore standard 0x29B1");

  uint8_t p[PKT_LEN];
  nodo::buildPacket(p, 17, RST_WATCHDOG, 1843, 6120, 9653, 3091, 2987);
  std::string o = run(p, PKT_LEN);
  EXPECT(PKT_LEN == 15 && has(o, "2,17,bme280,,18.43,61.20,965.3,3091,2987,-72,9\r\n"), "pacchetto normale; reset vuoto fuori da seq 0");

  nodo::buildPacket(p, 0, RST_WATCHDOG, -1234, 9999, 9700, 3200, ND_U16);
  o = run(p, PKT_LEN);
  EXPECT(has(o, "# nodo 2: riavvio (causa: watchdog)") && has(o, "2,0,bme280,4,-12.34,99.99,970.0,3200,,-72,9\r\n"),
         "avvio: causa watchdog, T negativa, Vtx non disponibile");

  nodo::buildPacket(p, 5, RST_ALIM, ND_I16, ND_U16, ND_U16, 2310, 2105);
  o = run(p, PKT_LEN);
  EXPECT(has(o, "il sensore non risponde") && has(o, "2,5,bme280,,,,,2310,2105,-72,9\r\n"), "sensore guasto: campi vuoti");

  nodo::buildPacket(p, 255, RST_NESSUNO, 2000, 5000, 9600, 3000, 2900);
  o = run(p, PKT_LEN);
  EXPECT(has(o, "2,255,bme280,"), "contatore 255");

  for (int i = 0; i < PKT_LEN; i++) {           // ogni singolo byte alterato va scartato
    nodo::buildPacket(p, 9, 0, 2000, 5000, 9600, 3000, 2900);
    p[i] ^= 0x10;
    o = run(p, PKT_LEN);
    if (!has(o, "CRC errato")) { printf("FALLITO byte %d alterato non rilevato\n", i); fails++; }
  }
  printf("ok    alterazione di ciascuno dei 15 byte rilevata dal CRC\n");

  nodo::buildPacket(p, 9, 0, 2000, 5000, 9600, 3000, 2900);
  o = run(p, PKT_LEN - 1);
  EXPECT(has(o, "CRC errato"), "pacchetto troncato scartato");

  uint8_t old10[10] = {2, 7, 0x07, 0x33, 0x17, 0xE8, 0x25, 0xB5, 0x0C, 0xEB};   // formato v0
  o = run(old10, 10);
  EXPECT(has(o, "scartato"), "pacchetto del formato precedente (10 byte) scartato");

  nodo::buildPacket(p, 9, 0, 2000, 5000, 9600, 3000, 2900);
  p[0] = 2 << 4 | 2; { uint16_t c = rx::crc16(p, 13); p[13] = c >> 8; p[14] = c; }
  o = run(p, PKT_LEN);
  EXPECT(has(o, "formato v2"), "versione sconosciuta scartata anche con CRC valido");

  uint8_t s[13] = {1 << 4 | 3, 4, 2 << 4, 0x07, 0x33, 0x17, 0xE8, 0x0B, 0xB8, 0x0B, 0x54};  // SHT45 nodo 3
  { uint16_t c = rx::crc16(s, 11); s[11] = c >> 8; s[12] = c; }
  o = run(s, 13);
  EXPECT(has(o, "3,4,sht45,,18.43,61.20,,3000,2900,-72,9\r\n"), "nodo SHT45 (13 byte): pressione vuota");

  nodo::buildPacket(p, 1, 0, 2000, 5000, 9600, 3000, 2900);
  uint32_t before = rx::nRadioErr;
  o = run(p, PKT_LEN, LORA_P2P_RXERROR);
  EXPECT(rx::nRadioErr == before + 1 && o.empty(), "callback con Status errore: ignorata, nessuna riga");

  out.clear(); uint32_t ov = rx::nOverflow;
  for (int i = 0; i < 9; i++) { rui_lora_p2p_recv_t d{p, PKT_LEN, -80, 5, LORA_P2P_RXDONE}; rx::recvCallback(d); }
  EXPECT(rx::nOverflow == ov + 2, "coda: 7 pacchetti accodati, 2 contati come coda piena");
  rx::qTail = rx::qHead;

  RCCs.CSR = RCC_CSR_IWDGRSTF | RCC_CSR_PINRSTF;  EXPECT(rx::readResetCause() == 4, "reset: watchdog prevale sul pin");
  RCCs.CSR = RCC_CSR_BORRSTF | RCC_CSR_PINRSTF;   EXPECT(rx::readResetCause() == 1, "reset: accensione/BOR");
  RCCs.CSR = RCC_CSR_SFTRSTF | RCC_CSR_PINRSTF;   EXPECT(rx::readResetCause() == 3, "reset: software");
  RCCs.CSR = RCC_CSR_PINRSTF;                     EXPECT(rx::readResetCause() == 2, "reset: solo pin");

  printf(fails ? "\n%d TEST FALLITI\n" : "\nTutti i test superati\n", fails);
  return fails != 0;
}
