#!/bin/sh
# Prove al banco del formato dei pacchetti, senza schede: estrae dagli sketch la
# costruzione del pacchetto (nodo) e la verifica/decodifica (ricevitore), le
# compila sul PC con stub minimi al posto di RUI3 ed esegue i test.
# Uso: sh esegui.sh   (serve un compilatore C++17 e python3)
set -e
cd "$(dirname "$0")"
python3 estrai.py ../nodo_bme280/nodo_bme280.ino ../ricevitore_p2p/ricevitore_p2p.ino estratto.inc
c++ -std=c++17 -Wall -Wextra -o banco banco.cpp
./banco
