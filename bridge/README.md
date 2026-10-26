# Bridge Raspberry → WordPress

Gira sul Raspberry Pi 3 della scuola, collegato via USB al ricevitore LoRa.
Legge le righe CSV del ricevitore, aggiunge data e ora, le conserva in una coda
locale e le invia al plugin [DB Stazione Meteo](https://github.com/dadebertolino/db-stazione-meteo) sul sito.

```
ricevitore ──USB──► seriale.py ──► decodifica.py ──► coda.py (SQLite) ──► invio.py ──HTTPS──► WordPress
                                    data e ora                              ogni 5 min, con token
```

**Stato:** lettura della seriale, coda SQLite e invio al sito funzionanti, provati con il ricevitore e con un WordPress locale. Script di installazione per il Raspberry pronto, da provare sul Pi.

## Prova sul PC

```bash
python3 -m pip install pyserial   # nello stesso Python che lancia il bridge
cd bridge
python3 -m meteo_bridge --porta /dev/cu.usbserial-XXXX --database coda.sqlite \
    --url http://sito-di-prova.local/wp-json/dbmt/v1/misure --token <token> --intervallo 60
```

Senza `--url` le misure restano in coda. L'indirizzo deve essere `https`: `http` è ammesso solo per `localhost` e i siti `.local`, perché il token viaggerebbe in chiaro.

Su Windows la porta è del tipo `COM5`. Ogni riga del ricevitore viene stampata; quelle di dati finiscono in coda con l'ora di arrivo in UTC:

```
2,41,bme280,,18.33,57.24,964.7,3000,3000,-51,14  [accodata, in coda: 12]
```

Si ferma con Ctrl+C: le misure restano nel file e il bridge le ritrova al riavvio. Per guardarle:

```bash
sqlite3 coda.sqlite "select ricevuto, nodo, seq, temp_c, ur_pct, press_hpa from misure order by id desc limit 10"
```

Il formato dei dati inviati e delle risposte è definito nel [README del plugin](https://github.com/dadebertolino/db-stazione-meteo#formato-dei-dati-bridge--plugin).

## Installazione sul Raspberry

Serve Raspberry Pi OS Lite (Bookworm o più recente), con SSH attivo e la rete. Sul Pi:

```bash
sudo apt install -y git
git clone https://github.com/dadebertolino/stazione-meteo-lora
sudo bash stazione-meteo-lora/bridge/installa.sh
```

Lo script installa Python e pyserial, crea l'utente di servizio `meteo`, copia il codice in `/opt/meteo-bridge`, scrive `/etc/meteo-bridge/config.toml` (leggibile solo da root e dal servizio, perché contiene il token), attiva NTP, **prova il collegamento con il sito** con un invio vuoto e avvia il servizio `meteo-bridge`. Chiede quello che non trova: la porta del ricevitore (se c'è un solo dispositivo USB lo riconosce da solo), l'indirizzo e il token, che si scrivono senza che compaiano a schermo.

| Opzione | Quando serve |
|---|---|
| `--porta /dev/serial0` | ricevitore collegato al GPIO invece che all'USB |
| `--url https://…/wp-json/dbmt/v1/misure` | per non doverlo scrivere a mano |
| `--ntp ntp.scuola.it` | la rete blocca i server NTP pubblici |
| `--proxy http://proxy:3128` | la rete esce su internet solo da un proxy |

Rilanciarlo aggiorna il codice e riavvia il servizio, tenendo la configurazione. Per aggiornare: `cd stazione-meteo-lora && git pull && sudo bash bridge/installa.sh`.

```bash
journalctl -u meteo-bridge -f          # log in diretta
sudo systemctl restart meteo-bridge    # riavvio, per esempio dopo aver modificato config.toml
```

## Ricevitore sul GPIO invece che sull'USB

Il ricevitore può stare sul connettore del Raspberry, senza adattatore USB. Il bridge legge soltanto, quindi basta un filo per i dati: **il TX del Pi resta scollegato**, così sul ricevitore non può arrivare nessun comando AT per errore.

| RAK3272S | Raspberry Pi 3 |
|---|---|
| VDD (3,3 V) | pin 1 (3,3 V) |
| GND | pin 6 (GND) |
| PA2, TX della UART2 | pin 10, GPIO15 (RXD) |

Per aggiornare il firmware del ricevitore si torna all'adattatore USB.

Sul Pi 3 la UART "buona" (PL011) è usata dal Bluetooth; quella che resta ha la velocità legata alla frequenza della CPU e perde caratteri. Quindi, una volta:

```bash
sudo raspi-config nonint do_serial_cons 1   # niente console di login sulla seriale
sudo raspi-config nonint do_serial_hw 0     # UART attiva
echo 'dtoverlay=disable-bt' | sudo tee -a /boot/firmware/config.txt
sudo systemctl disable hciuart
sudo reboot
```

Poi `sudo bash stazione-meteo-lora/bridge/installa.sh --porta /dev/serial0`.

## Moduli

| File | Ruolo |
|---|---|
| `meteo_bridge/__main__.py` | avvio: `python3 -m meteo_bridge --config ...` |
| `meteo_bridge/seriale.py` | lettura della seriale in sola lettura, ripresa se la porta sparisce |
| `meteo_bridge/decodifica.py` | riga CSV del ricevitore → misura; scarta le righe `#` e `+EVT` |
| `meteo_bridge/coda.py` | coda SQLite: le misure restano finché il sito non conferma |
| `meteo_bridge/invio.py` | POST a gruppi di 500 verso `/wp-json/dbmt/v1/misure`, in un thread, solo libreria standard |
| `meteo_bridge/orologio.py` | attesa della sincronizzazione NTP prima di registrare |
| `config.example.toml` | configurazione di esempio |
| `installa.sh` | installazione e aggiornamento sul Raspberry |
| `systemd/meteo-bridge.service` | servizio che parte all'avvio e riparte se si ferma; non può scrivere fuori da `/var/lib/meteo-bridge` |
| `tests/` | test senza ricevitore: `python3 -m unittest discover tests` |

## Scelte

- **Il Raspberry spinge, il sito riceve.** La connessione parte dalla scuola in HTTPS: non serve aprire porte né avere un IP pubblico.
- **Data e ora le mette il Raspberry**, all'arrivo della riga. Prima di registrare aspetta l'NTP: il Pi 3 non ha un orologio a batteria.
- **Nessuna misura persa** senza internet o con il sito fermo: la coda le conserva e le invia più tardi con l'ora originale.
- **La seriale si apre in sola lettura**: l'interprete AT del ricevitore è attivo e qualunque scrittura sulla porta verrebbe eseguita come comando.
- **Porta con nome stabile** (`/dev/serial/by-id/...`), non `/dev/ttyUSB0`.

## Requisiti

Raspberry Pi OS Bookworm (Python 3.11, per leggere la configurazione TOML):

```bash
sudo apt install python3-serial
```
