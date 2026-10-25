# Bridge Raspberry → WordPress

Gira sul Raspberry Pi 3 della scuola, collegato via USB al ricevitore LoRa.
Legge le righe CSV del ricevitore, aggiunge data e ora, le conserva in una coda
locale e le invia al plugin [DB Stazione Meteo](https://github.com/dadebertolino/db-stazione-meteo) sul sito.

```
ricevitore ──USB──► seriale.py ──► decodifica.py ──► coda.py (SQLite) ──► invio.py ──HTTPS──► WordPress
                                    data e ora                              ogni 5 min, con token
```

**Stato:** struttura, non ancora operativo.

Il formato dei dati inviati e delle risposte è definito nel [README del plugin](https://github.com/dadebertolino/db-stazione-meteo#formato-dei-dati-bridge--plugin).

## Moduli

| File | Ruolo |
|---|---|
| `meteo_bridge/__main__.py` | avvio: `python3 -m meteo_bridge --config ...` |
| `meteo_bridge/seriale.py` | lettura della seriale in sola lettura, ripresa se la porta sparisce |
| `meteo_bridge/decodifica.py` | riga CSV del ricevitore → misura; scarta le righe `#` e `+EVT` |
| `meteo_bridge/coda.py` | coda SQLite: le misure restano finché il sito non conferma |
| `meteo_bridge/invio.py` | POST a gruppi verso `/wp-json/dbmt/v1/misure` |
| `meteo_bridge/orologio.py` | attesa della sincronizzazione NTP prima di registrare |
| `config.example.toml` | configurazione di esempio |
| `systemd/meteo-bridge.service` | servizio che parte all'avvio e riparte se si ferma |
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
sudo apt install python3-serial python3-requests
```
