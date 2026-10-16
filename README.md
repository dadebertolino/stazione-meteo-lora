# Link LoRa

Rete di sensori **LoRa P2P** basata su moduli **RAK3172** (firmware RUI3 ≥ 4.1).
I nodi trasmettono le misure direttamente a un ricevitore, senza rete LoRaWAN e
senza gateway; il ricevitore le verifica e le stampa in CSV su seriale. È
indipendente dal datalogger MicroMET3.

```
nodo BME280 (ID 2) ──┐
                     │  LoRa P2P 868,1 MHz        seriale USB, CSV
nodo SHT45 (ID 1)* ──┴───────────────────► ricevitore ──────────────► PC o Raspberry
                                                                       registratore (prove)
                                                                       bridge Python*
```

\* non ancora realizzati.

| Componente | Ruolo | Stato |
|---|---|---|
| [`nodo_bme280/`](nodo_bme280/nodo_bme280.ino) | nodo temperatura, umidità, pressione con sensore BME280 | firmware 1.0, in prova |
| [`ricevitore_p2p/`](ricevitore_p2p/ricevitore_p2p.ino) | ricevitore: verifica e decodifica i pacchetti, li stampa in CSV | firmware 1.0, in prova |
| [`registratore/`](registratore/registra.py) | salva la seriale del ricevitore con l'ora di arrivo, per le prove | pronto |
| [`test_banco/`](test_banco/esegui.sh) | prove del formato dei pacchetti sul PC, senza schede | 17 test |
| nodo SHT45 | nodo temperatura e umidità | da scrivere quando arrivano i componenti; il ricevitore ne decodifica già il pacchetto |
| bridge Python | archivio dei dati, allarmi sui nodi muti | da scrivere |

Ogni sketch sta in una cartella con lo stesso nome del file `.ino`, come richiede
l'IDE Arduino. Si compilano con il pacchetto di schede RAKwireless RUI3
(verificati con la versione 4.3.0, scheda *WisDuo RAK3172 Evaluation Board*).

**Nodi e ricevitore vanno aggiornati insieme**: il formato dei pacchetti è
cambiato con il firmware 1.0 e il ricevitore scarta quelli del formato
precedente.

## Parametri radio

Devono essere **identici** su nodi e ricevitore. All'avvio ogni sketch fa
un'autodiagnosi: se il modulo è in modalità LoRaWAN passa a P2P e si riavvia, poi
confronta i parametri salvati in flash e corregge quelli diversi. Se il
passaggio a P2P non riesce lo segnala e prosegue, invece di riavviarsi in
continuazione.

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

A SF7 il tempo in aria cresce a gradini di 5 simboli (~5 ms). I pacchetti del
formato v1, 15 byte per il BME280 e 13 per l'SHT45, stanno sullo stesso gradino:
**circa 46 ms**.

| Intervallo | Duty cycle |
|---|---|
| 30 s (prova) | ~0,15 % |
| 10 min (esercizio) | ~0,008 % |

Il nodo non compila con un intervallo sotto i 10 s (`static_assert`): con 46 ms
in aria l'1 % ne richiederebbe almeno 4,6.

Fino all'ottobre 2026 la frequenza era 868,000 MHz: metà del segnale cadeva sotto
868,0 MHz, in una banda con duty cycle 0,1 %, superato con l'intervallo di prova.

I 14 dBm sono misurati all'uscita del modulo; il limite di 25 mW è riferito
alla potenza irradiata (ERP) e quindi include il guadagno dell'antenna. Con
un'antenna a stilo o a dipolo si resta nel limite; con un'antenna di guadagno
superiore a ~2 dBi la potenza va ridotta di conseguenza.

868,1 MHz è anche il primo canale di LoRaWAN. Se nella zona ci sono dispositivi
LoRaWAN possono esserci collisioni, visibili come salti nel contatore.

## Nodo BME280

### Alimentazione

**Due stilo AA alcaline** collegate direttamente al pin 3V3 del modulo, senza
regolatore: il RAK3172 funziona fra 2,0 e 3,6 V, e senza regolatore
`api.system.bat.get()` misura proprio la tensione delle batterie.

| Stato | Tensione (2 × AA) | Per cella |
|---|---|---|
| Nuove | 3,1-3,2 V | 1,55-1,6 V |
| A metà vita | ~2,5 V | ~1,25 V |
| Quasi scariche | ~2,2-2,3 V | ~1,1-1,15 V |
| Spegnimento del modulo | 2,0 V | 1,0 V |

Non usare stilo al litio (tipo Energizer L91): nuove arrivano a circa 3,6 V, al
limite massimo del modulo.

Il nodo trasmette due tensioni: **a riposo**, prima della trasmissione, e
**sotto carico**, misurata a metà della trasmissione. Con le alcaline la
differenza fra le due è il segnale più chiaro di batterie in esaurimento, e al
freddo cresce anche con batterie ancora buone. La tensione sotto carico è quella
della trasmissione *precedente*: quando la misura, il pacchetto è già partito.

All'avvio l'autodiagnosi confronta la tensione con due soglie e segnala un
errore se ne è fuori (solo su seriale, il nodo prosegue):

| Costante | Valore | Significato |
|---|---|---|
| `VBAT_MIN_MV` | 2300 mV | alcaline quasi scariche |
| `VBAT_MAX_MV` | 3600 mV | massimo del RAK3172: sopra si rischia di danneggiarlo |

### Collegamenti I2C

| BME280 | RAK3172 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | PA11 |
| SCL | PA12 |

Sui moduli a 6 pin, `CSB` va a 3V3 per selezionare l'I2C; `SDO` sceglie
l'indirizzo (a GND = `0x76`, a 3V3 = `0x77`). Lo sketch li prova entrambi e
riconosce un BMP280 montato per errore (niente umidità).

A batteria conviene un modulo **senza regolatore né traslatori di livello**: di
solito sono quelli a 6 pin. I moduli a 4 pin che li hanno consumano sempre,
anche quando il sensore dorme.

### Ciclo di funzionamento

1. Lettura del sensore in *forced mode* (una misura, poi il sensore torna a
   dormire) e della tensione a riposo.
2. Trasmissione del pacchetto; a metà trasmissione, lettura della tensione sotto
   carico per il pacchetto successivo.
3. Sleep per l'intervallo **±10 %, scelto a caso** a ogni ciclo: due nodi accesi
   insieme con lo stesso intervallo, altrimenti, trasmetterebbero sempre nello
   stesso istante e collidrebbero a ogni ciclo. Il seme del generatore casuale
   viene dall'identificativo unico del modulo (DevEUI).

| Costante | Prova | Esercizio |
|---|---|---|
| `INTERVAL_MS` | `30000UL` (30 s ± 3 s) | `600000UL` (10 min ± 1 min) |
| `NODE_DEBUG` | `1`: stampa su seriale | `0`: nessuna stampa |
| `NODE_ID` | 1-15, diverso per ogni nodo | |

Cambiare l'intervallo non richiede modifiche al ricevitore, che ascolta in
continuo.

### Robustezza

| Guasto | Cosa fa il nodo |
|---|---|
| **Sensore che non risponde**, misura non conclusa entro 100 ms, registri ai valori di reset (sensore riavviato) o valori fuori dal campo del BME280 (−40…85 °C, 300…1100 hPa) | sblocca il bus I2C, reinizializza il sensore e lo rilegge subito; se non va ancora trasmette il pacchetto con le misure **non disponibili** e riprova al ciclo successivo |
| **Bus I2C bloccato** (il sensore tiene SDA basso) | 9 impulsi su SCL e una condizione di STOP, poi riapre il bus |
| **Firmware bloccato** | **watchdog** a 8 s: il nodo si riavvia. Il watchdog continua a contare anche in sleep, quindi lo sleep lungo è diviso in tratti di 5 s con una ricarica fra l'uno e l'altro |
| **Sleep interrotto in anticipo** | il tempo si conta con `millis()`, che in RUI3 avanza anche in sleep: il nodo si riaddormenta invece di trasmettere prima del dovuto |
| **Riavvio** (calo di tensione, watchdog, reset) | il primo pacchetto dopo il riavvio ha contatore 0 e porta la causa del reset |

Un nodo con il sensore guasto continua a trasmettere tensioni e contatore: dal
ricevitore si distingue da un nodo spento, che non trasmette nulla.

## Formato dei pacchetti (v1)

Interi **big endian**. Ogni pacchetto finisce con un **CRC-16/CCITT-FALSE**
(polinomio 0x1021, valore iniziale 0xFFFF, controllo su "123456789" = 0x29B1)
calcolato su tutti i byte precedenti.

| Byte | BME280 (15 byte) | SHT45 (13 byte) |
|---|---|---|
| 0 | versione formato (4 bit alti, = 1) · ID nodo (4 bit bassi, 1-15) | uguale |
| 1 | contatore | uguale |
| 2 | tipo di nodo (4 bit alti: 1 = BME280, 2 = SHT45) · causa del reset (4 bit bassi) | uguale |
| 3-4 | temperatura °C × 100, con segno | uguale |
| 5-6 | umidità % × 100 | uguale |
| 7-8 | pressione hPa × 10 | tensione a riposo, mV |
| 9-10 | tensione a riposo, mV | tensione sotto carico, mV |
| 11-12 | tensione sotto carico (trasmissione precedente), mV | CRC-16 |
| 13-14 | CRC-16 | — |

La pressione è quella **misurata sul posto**, non riportata al livello del mare.

### Perché un CRC nel payload

La radio ha già un CRC hardware, attivo, ma in RUI3 4.3.0 un pacchetto che non
lo supera **arriva lo stesso allo sketch**. Il driver radio tratta separatamente
i due eventi che la radio segnala insieme, "pacchetto ricevuto" ed "errore CRC":

1. il payload corrotto viene consegnato alla callback come valido
   (`Status = LORA_P2P_RXDONE`);
2. la callback viene richiamata con `Status = LORA_P2P_RXERROR`, ma con il
   buffer del pacchetto precedente.

Il ricevitore scarta il secondo richiamo guardando `Status`, il primo con il CRC
del payload. Il CRC scarta anche i pacchetti di altri dispositivi che usano gli
stessi parametri radio. È una deduzione dai sorgenti del core
(`service_lora_p2p.c`, `radio.c`, `stm32wlxx_hal_subghz.c`), non ancora
osservata sulla scheda: vedi [Prove](#prove).

### Contatore e riavvii

Il contatore vale **0 solo nel primo pacchetto dopo un riavvio**, poi va da 1 a
255 e riparte da 1. Avanza anche se l'invio fallisce o il sensore è guasto: un
salto vuol dire "pacchetto previsto e non arrivato".

Fra due pacchetti ricevuti dallo stesso nodo i persi sono
`(seq_nuovo − seq_vecchio) mod 255 − 1`, contando sui valori 1-255. A 10 minuti
il contatore fa il giro in circa 42 ore: per silenzi più lunghi si usa l'ora di
arrivo.

Causa del reset, solo nel pacchetto con contatore 0:

| Codice | Causa |
|---|---|
| 1 | accensione o calo di tensione (*brown-out*) |
| 2 | pin NRST (tasto di reset) |
| 3 | software (es. passaggio a P2P dell'autodiagnosi) |
| 4 | watchdog |
| 5 | watchdog a finestra |
| 6 | errore di gestione del basso consumo |
| 7 | altro |

Il microcontrollore non distingue l'accensione da un calo di tensione: lo dice la
tensione nello stesso pacchetto. Con batterie inserite e cariche è
un'accensione; con tensione bassa, o con riavvii ripetuti, è un calo durante la
trasmissione e le batterie sono alla fine.

### Misure non disponibili

Valori riservati, fuori da qualsiasi misura reale:

| Campo | Valore riservato | Quando |
|---|---|---|
| temperatura | `0x8000` | sensore che non risponde |
| umidità, pressione | `0xFFFF` | sensore che non risponde |
| tensione sotto carico | `0xFFFF` | primo pacchetto dopo l'avvio, o trasmissione finita prima della misura |

## Uscita del ricevitore

Seriale a 115200 baud, una riga per pacchetto valido (valori di esempio):

```
# id,seq,tipo,reset,temp_C,ur_pct,press_hPa,vbat_mV,vbat_tx_mV,rssi,snr
# nodo 2: riavvio (causa: alimentazione)
2,0,bme280,1,18.43,61.20,965.3,3091,,-72,9
2,1,bme280,,18.40,61.10,965.2,3090,2987,-71,8
```

- Le misure non disponibili escono come **campi vuoti**; per i nodi SHT45
  `press_hPa` è sempre vuoto.
- `reset` è valorizzato solo con `seq` 0.
- Tutte le altre righe iniziano con `#` e vanno ignorate da chi legge i dati:
  autodiagnosi, avvisi (riavvio di un nodo, sensore guasto), pacchetti scartati
  e, ogni 10 minuti, le statistiche:

```
# stat: 120 validi, 0 CRC errato, 0 errori radio, 0 formato, 0 coda piena, 0 troppo lunghi
```

| Contatore | Significato |
|---|---|
| `CRC errato` | pacchetto corrotto, o di un altro dispositivo |
| `errori radio` | richiami della callback con `Status` di errore (vedi [Perché un CRC](#perché-un-crc-nel-payload)) |
| `formato` | CRC valido ma versione, tipo o lunghezza non riconosciuti |
| `coda piena` | pacchetti arrivati più in fretta di quanto il ricevitore li stampi |
| `troppo lunghi` | oltre 32 byte |

La callback di ricezione gira nell'interrupt della radio: lì il pacchetto viene
solo copiato in una coda di 7 posti, verifica e stampa avvengono in `loop()`.
Anche il ricevitore ha il watchdog a 8 s, e ogni 10 minuti riavvia la ricezione
per sicurezza.

## Registratore

[`registratore/registra.py`](registratore/registra.py) legge la seriale del
ricevitore e scrive, nella cartella indicata, due file al giorno:

| File | Contenuto |
|---|---|
| `AAAA-MM-GG.log` | tutte le righe, anche quelle `#`, precedute dall'ora di arrivo |
| `AAAA-MM-GG.csv` | solo le righe di dati, con l'ora di arrivo come prima colonna `ricevuto` |

```bash
pip install pyserial
python3 registra.py /dev/ttyUSB0 --cartella registrazioni
```

Su Windows la porta è del tipo `COM5`. Se la porta sparisce (cavo staccato,
ricevitore riavviato) riprova ogni 5 s e registra solo i cambi di stato. Si ferma
con Ctrl+C o `SIGTERM`.

L'ora è quella del computer: **deve essere sincronizzata** (NTP). Senza l'ora di
arrivo i dati delle prove servono a poco, come ha insegnato l'orologio della
MicroMET3.

## Prove

Verifiche fatte al banco, senza schede:

- i due sketch compilano senza avvisi (`--warnings all`), anche con
  `NODE_DEBUG 0`;
- costruzione del pacchetto del nodo e decodifica del ricevitore, estratte dagli
  sketch e compilate sul PC: valori normali e negativi, misure non disponibili,
  avvio con causa del reset, nodo SHT45, alterazione di ciascuno dei 15 byte,
  pacchetto troncato, formato precedente, versione sconosciuta, callback con
  errore, coda piena, priorità delle cause di reset;
- registratore su porta seriale virtuale: righe, file CSV, porta mancante,
  arresto.

Le prove sul formato dei pacchetti si rilanciano dopo ogni modifica agli sketch
(serve un compilatore C++ e python3):

```bash
sh "Link LORA/test_banco/esegui.sh"
```

Da provare sulle schede:

| Prova | Cosa guardare |
|---|---|
| Avvio del nodo con monitor seriale | autodiagnosi, causa reset "alimentazione", tensione |
| Ricezione a breve distanza | contatore senza salti, `vbat_tx_mV` presente e più bassa di `vbat_mV` |
| Sensore staccato a nodo acceso | righe con campi vuoti; riattaccandolo, le misure tornano senza riavviare |
| Tasto di reset del nodo | `seq` 0, causa 2 |
| Nodo lontano, al limite della portata | contatori `CRC errato` ed `errori radio` nelle statistiche: confermano o smentiscono il doppio richiamo descritto sopra |
| Consumo in sleep, con un multimetro in serie | dà l'autonomia reale; i risvegli ogni 5 s per il watchdog compaiono come brevi picchi |
| Alimentatore da banco abbassato piano da 3,2 a 2,0 V | comportamento a batterie scariche: riavvii, causa, tensioni |

## Punti aperti

| Questione | Dettaglio |
|---|---|
| **Doppio richiamo sugli errori CRC** | dedotto dai sorgenti, da confermare sulla scheda |
| **Tempo della misura sotto carico** | la tensione si legge 20 ms dopo l'avvio della trasmissione, che dura ~46 ms; quanto dura la lettura di `bat.get()` non è documentato. Se la trasmissione finisce prima, `vbat_tx_mV` resta vuota |
| **Comportamento a batterie scariche** | oggi il nodo trasmette finché si spegne; si potrebbe allungare l'intervallo sotto una soglia |
| **Bridge** | archivio, allarme sul nodo muto (nessun pacchetto per 2-3 intervalli), pacchetti persi, duplicati e riavvii per nodo |
