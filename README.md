# Link LoRa

Rete di sensori **LoRa P2P** basata su moduli **RAK3172** (firmware RUI3 ≥ 4.1).
I nodi trasmettono le misure direttamente a un ricevitore, senza rete LoRaWAN e
senza gateway; il ricevitore le stampa in CSV su seriale per un bridge Python sul
Raspberry. È indipendente dal datalogger MicroMET3.

```
nodo BME280 (ID 2) ──┐
                     │  LoRa P2P 868 MHz         seriale USB, CSV
nodo SHT45 (ID 1)* ──┴──────────────────► ricevitore ──────────────► Raspberry (bridge Python)*
```

\* non ancora realizzati.

| Sketch | Ruolo | Stato |
|---|---|---|
| [`nodo_bme280/`](nodo_bme280/nodo_bme280.ino) | nodo temperatura, umidità, pressione con sensore BME280 | in prova |
| [`ricevitore_p2p/`](ricevitore_p2p/ricevitore_p2p.ino) | ricevitore: decodifica i pacchetti e li stampa in CSV | in prova |
| nodo SHT45 | nodo temperatura e umidità | da scrivere quando arrivano i componenti; il ricevitore ne decodifica già il pacchetto |
| bridge Python | legge il CSV dal ricevitore sul Raspberry | da scrivere |

Ogni sketch sta in una cartella con lo stesso nome del file `.ino`, come richiede
l'IDE Arduino. Si compilano con il pacchetto di schede RAKwireless RUI3.

## Parametri radio

Devono essere **identici** su nodi e ricevitore. All'avvio ogni sketch fa
un'autodiagnosi: se il modulo è in modalità LoRaWAN passa a P2P e si riavvia, poi
confronta i parametri salvati in flash e corregge quelli diversi.

| Parametro | Valore |
|---|---|
| Frequenza | 868,000 MHz |
| Spreading factor | SF7 |
| Banda | 125 kHz |
| Coding rate | 4/5 |
| Preambolo | 8 simboli |
| Potenza | 14 dBm |

## Nodo BME280

Collegamenti I2C:

| BME280 | RAK3172 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | PA11 |
| SCL | PA12 |

Sui moduli a 6 pin, `CSB` va a 3V3 per selezionare l'I2C; `SDO` sceglie
l'indirizzo (a GND = `0x76`, a 3V3 = `0x77`). Lo sketch li prova entrambi e
riconosce un BMP280 montato per errore (niente umidità).

Il sensore è letto in *forced mode*, una misura per ciclo, poi il modulo va in
sleep. Configurazione in testa allo sketch:

| Costante | Prova | Esercizio |
|---|---|---|
| `INTERVAL_MS` | `30000UL` (30 s) | `600000UL` (10 min) |
| `DEBUG` | `1`: stampa su seriale | `0`: nessuna stampa |

Cambiare l'intervallo non richiede modifiche al ricevitore, che ascolta in
continuo.

## Formato dei pacchetti

Interi **big endian**. Il ricevitore distingue il tipo di nodo dalla lunghezza.

| Byte | BME280 (10 byte) | SHT45 (8 byte) |
|---|---|---|
| 0 | ID nodo | ID nodo |
| 1 | contatore (0…255, poi riparte) | contatore |
| 2-3 | temperatura °C × 100, con segno | temperatura °C × 100, con segno |
| 4-5 | umidità % × 100 | umidità % × 100 |
| 6-7 | pressione hPa × 10 | tensione mV |
| 8-9 | tensione mV | — |

La pressione è quella **misurata sul posto**, non riportata al livello del mare.

## Uscita del ricevitore

Seriale a 115200 baud, una riga per pacchetto valido (valori di esempio):

```
id,seq,temp_C,ur_pct,press_hPa,vbat_mV,rssi,snr
2,17,18.43,61.20,965.3,3291,-72,9
```

Per i nodi SHT45 il campo `press_hPa` resta vuoto. Tutte le altre righe
(autodiagnosi, statistiche, pacchetti scartati) iniziano con `#` e il bridge le
deve ignorare. Ogni 10 minuti il ricevitore stampa il conteggio dei pacchetti
validi e scartati e riavvia la ricezione.

I pacchetti persi si ricavano dai salti del contatore, che riparte da 0 dopo 255.

## Punti aperti

| Questione | Dettaglio |
|---|---|
| **Frequenza** | a 868,000 MHz con banda 125 kHz il segnale sconfina sotto 868,0 MHz, dove il duty cycle massimo è 0,1 %. Con invii ogni 10 minuti si resta ampiamente sotto; 868,1 MHz eviterebbe il problema in ogni caso |
| **Tensione misurata** | `api.system.bat.get()` sul RAK3172 dovrebbe misurare l'alimentazione del modulo, non della batteria: con un regolatore in mezzo leggerebbe sempre ~3,3 V. Da verificare |
| **Soglia di tensione alta** | `VBAT_MAX_MV` è 3700, ma il modulo accetta al massimo 3600 mV |
| **Sensore perso** | se il BME280 smette di rispondere dopo l'avvio, il nodo non ritenta l'inizializzazione |
| **Mittenti non filtrati** | il ricevitore accetta qualsiasi pacchetto di 8 o 10 byte; gli ID noti vanno filtrati nel ricevitore o nel bridge |
