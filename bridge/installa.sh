#!/bin/bash
# Installa o aggiorna il bridge sul Raspberry (Raspberry Pi OS, Debian).
#
#   git clone https://github.com/dadebertolino/stazione-meteo-lora
#   sudo bash stazione-meteo-lora/bridge/installa.sh
#
# Opzioni (facoltative, altrimenti le chiede):
#   --porta /dev/serial/by-id/...   porta del ricevitore (/dev/serial0 se è sul GPIO)
#   --url https://.../wp-json/dbmt/v1/misure
#   --ntp ntp.scuola.it             server NTP, se la rete blocca quelli pubblici
#   --proxy http://proxy:3128       proxy per uscire su internet, se la rete lo richiede
#
# Il token si chiede sempre a mano e non compare a schermo: così non resta
# nella cronologia della shell. Rilanciare lo script aggiorna il codice e
# riavvia il servizio; la configurazione esistente si tiene, salvo le voci
# passate come opzione.

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
CODICE=/opt/meteo-bridge
CONF_DIR=/etc/meteo-bridge
CONF=$CONF_DIR/config.toml
SERVIZIO=meteo-bridge

porta=""; url=""; ntp=""; proxy=""
while [ $# -gt 0 ]; do
	case "$1" in
		--porta) porta="$2"; shift 2 ;;
		--url)   url="$2";   shift 2 ;;
		--ntp)   ntp="$2";   shift 2 ;;
		--proxy) proxy="$2"; shift 2 ;;
		-h|--help) sed -n '2,17p' "$0"; exit 0 ;;
		*) echo "Opzione sconosciuta: $1" >&2; exit 2 ;;
	esac
done

passo() { printf '\n== %s\n' "$1"; }
errore() { printf 'ERRORE: %s\n' "$1" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || errore "serve sudo: sudo bash $0"
[ -d "$DIR/meteo_bridge" ] || errore "lanciare lo script dalla cartella bridge del repository"

passo "Pacchetti"
apt-get update -qq
apt-get install -y -qq python3 python3-serial >/dev/null
python3 -c 'import sys, tomllib' 2>/dev/null || errore "serve Python 3.11 o successivo (Raspberry Pi OS Bookworm o più recente)"
echo "Python $(python3 -c 'import platform; print(platform.python_version())'), pyserial $(python3 -c 'import serial; print(serial.__version__)')"

passo "Utente di servizio"
if ! id meteo >/dev/null 2>&1; then
	useradd --system --no-create-home --home-dir /var/lib/meteo-bridge --shell /usr/sbin/nologin meteo
	echo "creato l'utente meteo"
fi
usermod -a -G dialout meteo   # accesso alle porte seriali

passo "Codice in $CODICE"
mkdir -p "$CODICE"
rm -rf "$CODICE/meteo_bridge"
cp -r "$DIR/meteo_bridge" "$CODICE/"
find "$CODICE" -name __pycache__ -prune -exec rm -rf {} +
chown -R root:root "$CODICE"
echo "versione $(cd "$CODICE" && python3 -m meteo_bridge --version)"

passo "Configurazione $CONF"
mkdir -p "$CONF_DIR"
# Valore di una chiave nel file esistente (formato del config di esempio)
leggi() { [ -f "$CONF" ] && sed -n "s/^$1 *= *\"\\(.*\\)\"/\\1/p" "$CONF" | head -1 || true; }

if [ -z "$porta" ]; then
	porta="$(leggi porta)"
	if [ -z "$porta" ] || [ ! -e "$porta" ]; then
		mapfile -t trovate < <(ls /dev/serial/by-id/ 2>/dev/null | sed 's#^#/dev/serial/by-id/#')
		if [ "${#trovate[@]}" -eq 1 ]; then
			porta="${trovate[0]}"
			echo "ricevitore trovato su USB: $porta"
		else
			if [ "${#trovate[@]}" -gt 1 ]; then printf 'porte USB trovate:\n%s\n' "${trovate[@]}"; fi
			read -r -p "Porta del ricevitore (/dev/serial0 se è collegato al GPIO): " porta
		fi
	fi
fi
if [ ! -e "$porta" ]; then echo "ATTENZIONE: $porta non esiste adesso; il bridge la aspetterà"; fi

[ -n "$url" ] || url="$(leggi url)"
case "$url" in
	https://*) ;;
	*) read -r -p "Indirizzo dal sito (Impostazioni → Stazione Meteo), https://…/wp-json/dbmt/v1/misure: " url ;;
esac
case "$url" in https://*) ;; *) errore "l'indirizzo deve iniziare con https://" ;; esac

token="$(leggi token)"
if [ -z "$token" ]; then
	read -r -s -p "Token del sito (non compare mentre lo scrivi): " token; echo
else
	read -r -s -p "Token del sito (Invio per tenere quello salvato): " nuovo; echo
	if [ -n "$nuovo" ]; then token="$nuovo"; fi
fi
[ -n "$token" ] || errore "manca il token"

umask 027
cat > "$CONF" <<EOF
# Scritto da installa.sh il $(date '+%d/%m/%Y %H:%M'). Modifiche a mano: poi
# sudo systemctl restart $SERVIZIO

[seriale]
porta = "$porta"
baud = 115200

[coda]
database = "/var/lib/meteo-bridge/coda.sqlite"

[sito]
url = "$url"
token = "$token"
intervallo_invio_s = 300
EOF
chown root:meteo "$CONF"
chmod 640 "$CONF"   # contiene il token: leggibile solo da root e dal servizio
echo "salvata (porta $porta)"

passo "Orologio"
if [ -n "$ntp" ]; then
	mkdir -p /etc/systemd/timesyncd.conf.d
	printf '[Time]\nNTP=%s\n' "$ntp" > /etc/systemd/timesyncd.conf.d/meteo.conf
	echo "server NTP: $ntp"
fi
timedatectl set-timezone Europe/Rome
timedatectl set-ntp true
systemctl restart systemd-timesyncd
sleep 3
if [ "$(timedatectl show -p NTPSynchronized --value)" = "yes" ]; then
	echo "sincronizzato: $(date '+%d/%m/%Y %H:%M:%S %Z')"
else
	echo "ATTENZIONE: orologio non ancora sincronizzato. Il bridge non registra"
	echo "finché non lo è; se dura più di qualche minuto la rete blocca NTP:"
	echo "rilanciare con --ntp e il server indicato dal tecnico della scuola."
fi

passo "Collegamento con il sito"
if [ -n "$proxy" ]; then export https_proxy="$proxy"; fi
# Invio vuoto: verifica indirizzo e token senza aggiungere misure
esito="$(URL="$url" TOKEN="$token" python3 - <<'EOF'
import json, os, urllib.error, urllib.request
r = urllib.request.Request(os.environ["URL"], data=b'{"misure":[]}', method="POST", headers={
    "Content-Type": "application/json", "X-DBMT-Token": os.environ["TOKEN"],
    "User-Agent": "meteo-bridge/installa"})
try:
    with urllib.request.urlopen(r, timeout=20) as risposta:
        print("OK", risposta.status)
except urllib.error.HTTPError as e:
    print("HTTP", e.code)
except Exception as e:
    print("RETE", e)
EOF
)"
case "$esito" in
	"OK 200") echo "il sito risponde e accetta il token" ;;
	"HTTP 401") echo "ATTENZIONE: il sito rifiuta il token: rigeneralo e rilancia lo script" ;;
	"HTTP 404") echo "ATTENZIONE: indirizzo sbagliato o plugin non attivo sul sito" ;;
	*) echo "ATTENZIONE: sito non raggiungibile ($esito); rete, proxy o firewall della scuola?" ;;
esac

passo "Servizio $SERVIZIO"
install -m 644 "$DIR/systemd/$SERVIZIO.service" /etc/systemd/system/
if [ -n "$proxy" ]; then
	mkdir -p /etc/systemd/system/$SERVIZIO.service.d
	printf '[Service]\nEnvironment=https_proxy=%s\n' "$proxy" > /etc/systemd/system/$SERVIZIO.service.d/proxy.conf
	echo "proxy: $proxy"
fi
systemctl daemon-reload
systemctl enable --quiet $SERVIZIO
systemctl restart $SERVIZIO
sleep 3
systemctl --no-pager --lines=8 status $SERVIZIO || true

cat <<EOF

Fatto. Il bridge parte da solo a ogni accensione e riparte se si ferma.
  Log in diretta:      journalctl -u $SERVIZIO -f
  Riavvio:             sudo systemctl restart $SERVIZIO
  Misure in coda:      sudo -u meteo python3 -c "import sqlite3; print(sqlite3.connect('/var/lib/meteo-bridge/coda.sqlite').execute('select stato, count(*) from misure group by stato').fetchall())"
  Aggiornare:          cd stazione-meteo-lora && git pull && sudo bash bridge/installa.sh
EOF
