# Hardware

Progetti KiCad delle schede della stazione. Licenza CERN-OHL-S v2
([`LICENSE`](LICENSE)): chi modifica e distribuisce questi progetti deve
pubblicarne i sorgenti con la stessa licenza.

| Scheda | Cartella | Stato |
|---|---|---|
| Nodo BME280: RAK3172, BME280, 2 stilo AA | [`nodo-bme280/`](nodo-bme280/) | schema 0.1 (ERC senza errori), circuito stampato da disegnare |
| Ricevitore sul GPIO del Raspberry Pi | [`ricevitore/`](ricevitore/) | progetto vuoto; collegamenti in [`bridge/README.md`](../bridge/README.md#ricevitore-sul-gpio-invece-che-sullusb) |
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
│   ├── sym-lib-table               → librerie/stazione-meteo.kicad_sym
│   └── fp-lib-table                → librerie/stazione-meteo.pretty
└── ricevitore/                     stessa struttura
```

I file sono in formato **KiCad 9** e si aprono anche con le versioni successive. Il circuito stampato (`.kicad_pcb`) non c'è ancora: lo crea KiCad la prima volta che si apre l'editor PCB dal progetto. Ogni progetto trova la libreria condivisa con un percorso relativo (`${KIPRJMOD}/../librerie/`), quindi funziona dovunque si cloni il repository.

**Librerie**: il RAK3172 è nella libreria ufficiale di KiCad, simbolo e impronta (`RF_Module:RAK3172…`). In `librerie/` vanno solo le parti che mancano lì, per esempio la breakout RAK3272S: nessun progetto pubblico ne ha simbolo e impronta.

## Progetti di riferimento

| Progetto | Licenza | Da guardare |
|---|---|---|
| [teapotlaboratories/feather-rak3172](https://github.com/teapotlaboratories/feather-rak3172) | MIT | come si disegna attorno al RAK3172 un circuito a basso consumo: alimentazione, antenna (connettore coassiale), pulsante di reset, connettore SWD, misure di consumo nella documentazione. KiCad 6 |
| [ElektorLabs/lorawan-sensor-node-v2](https://github.com/ElektorLabs/lorawan-sensor-node-v2) | GPL-3.0 | un nodo sensore completo con RAK3172 diviso in fogli gerarchici (RAK3172, sensori, alimentazione), con PDF dello schema e distinta. KiCad 9 |

**Cosa si può riprendere.** Da feather-rak3172 (MIT) si possono copiare parti dello schema o del circuito stampato, citando la fonte e mantenendo l'avviso di licenza MIT. Il progetto Elektor (GPL-3.0) va solo **studiato**: copiarne parti in un progetto CERN-OHL-S metterebbe insieme due licenze reciproche diverse. Le soluzioni che ne prendiamo si ridisegnano da zero.

## Nodo BME280, schema 0.1

| Scelta | Soluzione |
|---|---|
| Modulo radio | RAK3172 saldato, versione con connettore IPEX (`RF_Module:RAK3172`); programmazione da J2 (UART2, BOOT0, NRST) e recupero da J3 (SWD) |
| Sensore | modulo BME280 a 4 pin su connettore femmina J1, nell'ordine VIN, GND, SCL, SDA; I²C su PA12 (SCL) e PA11 (SDA) |
| Batterie | portabatterie 2 × AA Keystone 2462 sulla scheda, nessun regolatore |
| Inversione delle batterie | Q1 **DMG2305UX**, P-MOSFET sul positivo (drain verso le batterie, source verso il circuito, gate a massa con R2): VGS(th) da −0,5 a −0,9 V, RDS(on) massima 200 mΩ a VGS = −1,8 V ([datasheet Diodes DS36196 Rev. 8-2](https://www.diodes.com/assets/Datasheets/DMG2305UX.pdf)), quindi meno di 10 mV di caduta a 45 mA anche con le batterie quasi scariche |
| Antenna | connettore u.FL del modulo e cavetto verso SMA a pannello: nessuna pista RF sulla scheda |

Da verificare: codice d'ordine della variante RAK3172 con IPEX; tensione al sensore e consumo a riposo del modulo BME280 a 4 pin (quasi sempre ha un regolatore a 3,3 V, che con le batterie sotto 3,3 V lavora in caduta).

## Scelte ancora aperte

| Domanda | Possibilità |
|---|---|
| Ricevitore | scheda tipo HAT sul connettore a 40 pin del Raspberry, con la breakout RAK3272S su zoccolo; dal Pi solo 3,3 V, GND e il pin 10 (RXD) |
| Contenitore | misure del contenitore stagno e dello schermo per il sensore: decidono la forma della scheda |
