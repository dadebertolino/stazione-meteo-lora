# Hardware

Progetti KiCad delle schede della stazione. Licenza CERN-OHL-S v2
([`LICENSE`](LICENSE)): chi modifica e distribuisce questi progetti deve
pubblicarne i sorgenti con la stessa licenza.

| Scheda | Cartella | Stato |
|---|---|---|
| Nodo BME280: RAK3172, BME280, 2 stilo AA | [`nodo-bme280/`](nodo-bme280/) | schema 0.2 (ERC senza errori), circuito stampato 0.2 per la scatola IP65 da 115 × 90 × 55 mm (DRC senza errori, fori da confermare sulla scatola) |
| Ricevitore USB, per PC o Raspberry Pi | [`ricevitore/`](ricevitore/) | schema 0.2 con USB-B (ERC senza errori, una segnalazione esclusa e motivata), circuito stampato 0.2 (DRC senza errori, da rivedere prima dell'ordine) |
| Nodo SHT45 | — | da progettare quando arrivano i componenti |

## Struttura

```
hardware/
├── librerie/
│   ├── stazione-meteo.kicad_sym    simboli nostri, condivisi dalle schede
│   └── stazione-meteo.pretty/      impronte nostre
├── nodo-bme280/
│   ├── nodo-bme280.kicad_pro
│   ├── nodo-bme280.kicad_sch
│   ├── nodo-bme280.kicad_pcb       circuito stampato
│   ├── nodo-bme280.kicad_dru       regole DRC aggiuntive
│   ├── nodo-bme280-schema.pdf      schema esportato, da rigenerare a ogni modifica
│   ├── sym-lib-table               → librerie/stazione-meteo.kicad_sym
│   └── fp-lib-table                → librerie/stazione-meteo.pretty
└── ricevitore/                     stessa struttura
```

Gli schemi sono in formato **KiCad 9** e si aprono anche con le versioni successive; i circuiti stampati sono in formato **KiCad 10**, quindi serve KiCad 10 o successivo. Ogni progetto trova la libreria condivisa con un percorso relativo (`${KIPRJMOD}/../librerie/`), quindi funziona dovunque si cloni il repository.

**Librerie**: il RAK3172 è nella libreria ufficiale di KiCad, simbolo e impronta (`RF_Module:RAK3172…`). In `librerie/` vanno solo le parti che mancano lì, per esempio la breakout RAK3272S: nessun progetto pubblico ne ha simbolo e impronta.

## Progetti di riferimento

| Progetto | Licenza | Da guardare |
|---|---|---|
| [teapotlaboratories/feather-rak3172](https://github.com/teapotlaboratories/feather-rak3172) | MIT | come si disegna attorno al RAK3172 un circuito a basso consumo: alimentazione, antenna (connettore coassiale), pulsante di reset, connettore SWD, misure di consumo nella documentazione. KiCad 6 |
| [ElektorLabs/lorawan-sensor-node-v2](https://github.com/ElektorLabs/lorawan-sensor-node-v2) | GPL-3.0 | un nodo sensore completo con RAK3172 diviso in fogli gerarchici (RAK3172, sensori, alimentazione), con PDF dello schema e distinta. KiCad 9 |

**Cosa si può riprendere.** Da feather-rak3172 (MIT) si possono copiare parti dello schema o del circuito stampato, citando la fonte e mantenendo l'avviso di licenza MIT. Il progetto Elektor (GPL-3.0) va solo **studiato**: copiarne parti in un progetto CERN-OHL-S metterebbe insieme due licenze reciproche diverse. Le soluzioni che ne prendiamo si ridisegnano da zero.

## Nodo BME280, schema 0.2

| Scelta | Soluzione |
|---|---|
| Modulo radio | RAK3172 saldato, versione con connettore IPEX (`RF_Module:RAK3172`); programmazione da J2 (UART2, BOOT0, NRST) e recupero da J3 (SWD) |
| Sensore | modulo BME280 a 4 pin su connettore femmina J1, nell'ordine VIN, GND, SCL, SDA; I²C su PA12 (SCL) e PA11 (SDA) |
| Batterie | portabatterie 2 × AA Keystone 2462 sulla scheda (BT1), nessun regolatore. In alternativa, dalla 0.2, un portabatterie 2 × AA filato su J4, in parallelo a BT1 e quindi anche lui dietro Q1: se ne monta uno solo |
| Inversione delle batterie | Q1 **DMG2305UX**, P-MOSFET sul positivo (drain verso le batterie, source verso il circuito, gate a massa con R2): VGS(th) da −0,5 a −0,9 V, RDS(on) massima 200 mΩ a VGS = −1,8 V ([datasheet Diodes DS36196 Rev. 8-2](https://www.diodes.com/assets/Datasheets/DMG2305UX.pdf)), quindi meno di 10 mV di caduta a 45 mA anche con le batterie quasi scariche |
| Antenna | connettore u.FL del modulo e cavetto verso SMA a pannello: nessuna pista RF sulla scheda |

Da verificare: codice d'ordine della variante RAK3172 con IPEX; tensione al sensore e consumo a riposo del modulo BME280 a 4 pin (quasi sempre ha un regolatore a 3,3 V, che con le batterie sotto 3,3 V lavora in caduta).

## Nodo BME280, circuito stampato 0.2

| Voce | Valore |
|---|---|
| Scatola | stagna IP65 in ABS da 115 × 90 × 55 mm esterni (LeMotech e altri, su Amazon): dentro circa 105–109 × 80–84 × 45–49 mm secondo il venditore |
| Scheda | 95 × 70 mm, 2 strati, FR4 1,6 mm; fori M3 (H1–H4) a 4 mm dagli angoli |
| Piazzamento | in alto il RAK3172 con J2 (UART2) sopra, J3 (SWD) e J1 (BME280) a sinistra, C1–C3, R1 e RESET a destra; in basso il portabatterie, con Q1 e R2 accanto al polo positivo e J4 sotto di loro. Tutti i componenti sul lato superiore |
| Regole | come il ricevitore: isolamento 0,2 mm, piste 0,25 mm, 0,4 mm per GND, +BATT e BATT_IN, vie 0,6/0,3 mm |
| Massa | il lato inferiore è un piano GND intero: tutte le piste stanno sopra |
| Portabatterie filato | J4: due piazzole per i fili (+ in alto, − in basso), con un foro da 2,2 mm accanto a ciascuna, verso il bordo, da cui far passare il filo prima di saldarlo, così non tira sulla saldatura (`Connector_Wire:SolderWire-0.25sqmm_1x02_P4.2mm_D0.65mm_OD1.7mm_Relief`). Il portabatterie filato si appoggia al posto di BT1 e si blocca con una fascetta fino a 3,6 mm di larghezza nelle asole H5 e H6 (4 × 1,6 mm, non metallizzate), sopra e sotto il corpo di BT1 a metà della sua lunghezza |

**DRC.** Nessuna violazione, nessun collegamento mancante, nessuna differenza dallo schema. `nodo-bme280.kicad_dru` accetta un solo raggio sul lato superiore per le piazzole passanti a GND, che sotto sono collegate con tutti i raggi.

**Prima dell'ordine.**
- Controllare il portabatterie filato: le asole sono a 36 mm di distanza (bordi interni 34,4 mm), pensate per un 2 × AA da circa 57 × 32 mm come BT1. Se è più largo, vanno allontanate.
- Misurare la scatola arrivata: le misure interne e la posizione delle colonnine cambiano da un venditore all'altro. I fori H1–H4 vanno spostati sulle colonnine, oppure la scheda si fissa con distanziali.
- Il BME280 va fuori dalla scatola, in uno schermo antiradiazione, con un cavo a 4 fili da un pressacavo: per un cavo, al posto della femmina a 4 poli in J1 conviene un morsetto a vite o un connettore JST con lo stesso passo.
- Il cavetto u.FL del RAK3172 va al connettore SMA a pannello sulla parete: lasciare spazio sopra il modulo.

## Ricevitore USB, schema 0.2

Una chiavetta che si collega con lo stesso cavo USB al PC o al Raspberry Pi: il sistema la vede come porta seriale, e il bridge funziona com'è.

| Parte | Soluzione |
|---|---|
| Connettore | USB-B femmina a 90°, a foro passante (Jing Extension 921-111A1010D10200, LCSC C46392), cavo A–B come le stampanti. Nella 0.1 era una USB-C (HRO TYPE-C-31-M-12) con R1 e R2 da 5,1 kΩ su CC: tolta perché a passo 0,5 mm è difficile da saldare a mano e da ordinare. La USB-B non ha le linee CC, quindi R1 e R2 non ci sono più; gli altri riferimenti restano com'erano |
| Protezione | USBLC6-2SC6 sulle linee dati |
| Alimentazione | AP2112K-3.3, da VBUS a 3,3 V per modulo e convertitore |
| USB-seriale | CH340C alimentato a 3,3 V (V3 su VCC, come da datasheet): la UART ha già i livelli del RAK3172. DTR e RTS non collegati, perché il bridge (pyserial) li attiva aprendo la porta e non deve resettare il modulo |
| Modulo | RAK3172 con IPEX, antenna su u.FL; seriale su UART2 come ora |
| Programmazione | dalla stessa USB, come adesso con l'adattatore; se il bootloader non risponde: tenere BOOT, premere e rilasciare RESET; oppure SWD su J2 |
| LED | D1 alimentazione; D2 su PA8, da far lampeggiare dal firmware del ricevitore a ogni pacchetto |

**Esclusione ERC.** Il pin V3 del CH340C è definito come uscita di alimentazione (è l'uscita del suo regolatore interno); a 3,3 V il datasheet lo vuole collegato a VCC, quindi sulla stessa rete dell'uscita di U3. L'ERC lo segnala come due uscite collegate: la segnalazione è esclusa, con la motivazione, nel `.kicad_pro`. Le altre regole restano attive.

**Sul Raspberry** il CH340 non ha numero di serie: in `/dev/serial/by-id/` comparirà con un nome come `usb-1a86_USB_Serial-if00-port0`, da verificare con `ls /dev/serial/by-id/` e da rimettere nel `config.toml` del bridge al posto di quello dell'adattatore FTDI.

## Ricevitore USB, circuito stampato 0.2

| Voce | Valore |
|---|---|
| Scheda | 60 × 25 mm, 2 strati, FR4 1,6 mm |
| Piazzamento | USB-B sul bordo sinistro, che sporge di circa 8 mm come sull'Arduino Uno: sulla scheda restano 9 mm di corpo e le linguette dello schermo, a 2,5 mm dal bordo; poi USBLC6 e regolatore, CH340C, RAK3172 al centro; a destra RESET (SW1), SWD (J2) e BOOT (SW2); LED in basso. Tutti i componenti sul lato superiore |
| Regole | isolamento 0,2 mm; piste 0,25 mm, 0,4 mm per VBUS, +3V3 e GND (classe `Alimentazione`); vie 0,6/0,3 mm: nei limiti standard di JLCPCB e PCBWay |
| Massa | piano GND su tutti e due gli strati; il lato inferiore è quasi intero (46 mm di piste in tutto) e ogni piazzola GND vi arriva con una via |
| Sbroglio | [Freerouting](https://github.com/freerouting/freerouting) 1.9.0 da un'esportazione Specctra per la 0.1; nella 0.2 VBUS, D+ e D− attorno alla USB-B sono tracciati a mano. Ogni volta controllo DRC con confronto sullo schema |

**DRC.** Nessun errore, nessun collegamento mancante, nessuna differenza dallo schema. Restano due avvisi di serigrafia tagliata dal bordo: sono le linee del corpo della USB-B, che sporge dal bordo. In `ricevitore.kicad_dru` c'è una regola sola: i piedini GND dei pulsanti hanno un solo raggio verso il piano superiore, perché sul lato inferiore il piano li raggiunge con tutti i raggi.

**Da guardare prima dell'ordine.**
- Le linee USB D+ e D− passano sul lato inferiore dalla USB-B fino a U2 (e D+ fino a U4), sotto la pista di VBUS: a 12 Mbit/s non serve l'impedenza controllata, ma conviene che siano corte e vicine.
- Sotto il RAK3172 ci sono due vie di massa (vicino ai pin 11 e 17): le vie sono coperte dal solder mask su tutti e due i lati, ma se il fondo del modulo ha parti scoperte vanno spostate.
- Nessun foro di fissaggio: dipende dal contenitore.

## File per la produzione

In ogni cartella delle schede, generati con KiCad 10.0.7 dal `.kicad_pcb` e da rigenerare a ogni modifica:

| File | Contenuto |
|---|---|
| `gerber/` e `<scheda>-gerber-0.2.zip` | rame, solder mask e serigrafia dei due lati, pasta superiore, contorno; foratura Excellon in mm, fori metallizzati e no nello stesso file |
| `<scheda>-bom-jlcpcb.csv` | distinta per il montaggio JLCPCB, solo SMD, con i codici LCSC; senza il RAK3172 |
| `<scheda>-cpl-jlcpcb.csv` | posizioni e rotazioni degli SMD, lato superiore |

JLCPCB monta solo gli SMD: i passanti (pulsanti, pin header, USB-B del ricevitore, femmina del BME280, portabatterie) farebbero salire molto il costo del montaggio e si saldano a mano, come il RAK3172-8-SM-I (C19723904), che a fine ottobre 2026 è esaurito su LCSC. Le rotazioni di U2, U3, U4 e Q1 sono corrette rispetto a KiCad per i modelli di JLCPCB, da ricontrollare comunque nell'anteprima. I Gerber del nodo vanno rifatti dopo aver spostato i fori sulle colonnine della scatola.

**Stencil unico.** `stencil-nodo-ricevitore-F_Paste.gtp` (e lo zip `stencil-nodo-ricevitore-0.2.zip`, con il ricevitore 0.2) mette in un solo Gerber, senza contorno, le paste superiori delle due schede: il nodo nella sua posizione, il ricevitore 75 mm più in basso, 5 mm sotto il bordo del nodo. Le aperture del ricevitore sono rinumerate da D110. Si rigenera dalle due `F_Paste` dopo ogni modifica delle schede.

## Scelte ancora aperte

| Domanda | Possibilità |
|---|---|
| Contenitore | misure del contenitore stagno e dello schermo per il sensore: decidono la forma della scheda del nodo |
