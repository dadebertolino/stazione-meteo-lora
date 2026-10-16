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
l'IDE Arduino. Si compilano con il pacchetto di schede RAKwireless RUI3
(verificati con la versione 4.3.0, scheda *WisDuo RAK3172 Evaluation Board*).

## Parametri radio

Devono essere **identici** su nodi e ricevitore. All'avvio ogni sketch fa
un'autodiagnosi: se il modulo è in modalità LoRaWAN passa a P2P e si riavvia, poi
confronta i parametri salvati in flash e corregge quelli diversi.

| Parametro | Valore |
|---|---|
| Frequenza | 868,100 MHz |
| Spreading factor | SF7 |
| Banda | 125 kHz |
| Coding rate | 4/5 |
| Preambolo | 8 simboli |
| Potenza | 14 dBm |

### Limiti di banda

Con banda 125 kHz centrata su 868,1 MHz il segnale occupa 868,0375-868,1625 MHz,
tutto dentro la sottobanda **868,0-868,6 MHz** (ERC/REC 70-03), che ammette
**25 mW ERP** e **duty cycle 1 %**: al massimo 36 s di trasmissione per ora.

Un pacchetto di 8 o 10 byte a SF7 dura circa 36 ms, quindi:

| Intervallo | Duty cycle |
|---|---|
| 30 s (prova) | ~0,12 % |
| 10 min (esercizio) | ~0,006 % |

Fino all'ottobre 2026 la frequenza era 868,000 MHz: metà del segnale cadeva sotto
868,0 MHz, in una banda con duty cycle 0,1 %, superato con l'intervallo di prova.

I 14 dBm sono misurati all'uscita del modulo; il limite di 25 mW è riferito
alla potenza irradiata (ERP) e quindi include il guadagno dell'antenna. Con
un'antenna a stilo o a dipolo si resta nel limite; con un'antenna di guadagno
superiore a ~2 dBi la potenza va ridotta di conseguenza.

868,1 MHz è anche il primo canale di LoRaWAN. Se nella zona ci sono dispositivi
LoRaWAN possono esserci collisioni, visibili come salti nel contatore; eventuali
pacchetti estranei di 8 o 10 byte che il ricevitore dovesse decodificare vanno
filtrati per ID (vedi [Punti aperti](#punti-aperti)).

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
sleep.

Una lettura è scartata se il sensore non risponde sull'I2C, se la misura non
termina entro 100 ms o se i registri contengono ancora i valori di reset (il
sensore si è riavviato, per esempio per un falso contatto sull'alimentazione).
In questi casi il nodo reinizializza il sensore e lo rilegge subito; se non
risponde ancora trasmette comunque il pacchetto, con le misure **non
disponibili** e la tensione (vedi [Formato dei pacchetti](#formato-dei-pacchetti)),
e ci riprova al ciclo successivo. Così il ricevitore distingue un sensore
guasto (pacchetti senza misure) da un nodo spento (nessun pacchetto).

Configurazione in testa allo sketch:

| Costante | Prova | Esercizio |
|---|---|---|
| `INTERVAL_MS` | `30000UL` (30 s) | `600000UL` (10 min) |
| `DEBUG` | `1`: stampa su seriale | `0`: nessuna stampa |

Cambiare l'intervallo non richiede modifiche al ricevitore, che ascolta in
continuo.

All'avvio l'autodiagnosi confronta la tensione di alimentazione con due soglie e
segnala un errore se ne è fuori:

| Costante | Valore | Significato |
|---|---|---|
| `VBAT_MIN_MV` | 2800 mV | alimentazione bassa; il modulo funziona fino a 2000 mV |
| `VBAT_MAX_MV` | 3600 mV | massimo di alimentazione del RAK3172 (2,0-3,6 V): sopra si rischia di danneggiarlo |

Il controllo è solo segnalato su seriale e non blocca il nodo.

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

Valori riservati per **misura non disponibile** (sensore che non risponde),
scelti fuori da qualsiasi misura reale:

| Campo | Valore riservato | Come apparirebbe se decodificato come misura |
|---|---|---|
| temperatura | `0x8000` | −327,68 °C |
| umidità | `0xFFFF` | 655,35 % |
| pressione | `0xFFFF` | 6553,5 hPa |

La lunghezza del pacchetto non cambia, quindi il tipo di nodo resta riconoscibile.
Il futuro nodo SHT45 deve usare la stessa convenzione. Nodo e ricevitore vanno
aggiornati insieme: un ricevitore precedente stamperebbe i valori della terza
colonna come se fossero misure.

## Uscita del ricevitore

Seriale a 115200 baud, una riga per pacchetto valido (valori di esempio):

```
id,seq,temp_C,ur_pct,press_hPa,vbat_mV,rssi,snr
2,17,18.43,61.20,965.3,3291,-72,9
```

Le misure non disponibili escono come **campi vuoti**; per i nodi SHT45
`press_hPa` è sempre vuoto. Un pacchetto con temperatura e umidità vuote viene
da un nodo acceso con il sensore guasto:

```
# nodo 2: il sensore non risponde
2,18,,,,3290,-71,9
```

Tutte le altre righe (autodiagnosi, statistiche, pacchetti scartati, avvisi
come quello sopra) iniziano con `#` e il bridge le deve ignorare. Ogni 10 minuti il ricevitore stampa il conteggio dei pacchetti
validi e scartati e riavvia la ricezione.

I pacchetti persi si ricavano dai salti del contatore, che riparte da 0 dopo 255.

## Punti aperti

| Questione | Dettaglio |
|---|---|
| **Tensione misurata** | `api.system.bat.get()` sul RAK3172 dovrebbe misurare l'alimentazione del modulo, non della batteria: con un regolatore in mezzo leggerebbe sempre ~3,3 V. Da verificare |
| **Macro `DEBUG`** | il pacchetto RUI3 definisce già `DEBUG` a 1 (`-DDEBUG`): con `DEBUG 0` lo sketch funziona ma il compilatore segnala la ridefinizione. Un nome proprio (es. `NODE_DEBUG`) eliminerebbe l'avviso |
| **Bus I2C bloccato** | se il sensore resta a metà di una trasmissione e tiene SDA basso, la reinizializzazione non basta: servirebbe la procedura di sblocco del bus (impulsi su SCL) |
| **Mittenti non filtrati** | il ricevitore accetta qualsiasi pacchetto di 8 o 10 byte; gli ID noti vanno filtrati nel ricevitore o nel bridge |
