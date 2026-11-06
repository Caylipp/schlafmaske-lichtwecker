#!/bin/zsh
# Firmware kompilieren und auf die Maske (XIAO ESP32C3) laden.
# Maske per USB-C anschließen, kurz vorher RESET drücken (sonst schläft sie und ist nicht erreichbar).
set -e
cd "$(dirname "$0")"
FQBN=esp32:esp32:XIAO_ESP32C3

if [[ ! -f lichtwecker/secrets.h ]]; then
  echo "Fehlt: lichtwecker/secrets.h (Vorlage: secrets.example.h)"; exit 1
fi

echo "Kompiliere ..."
arduino-cli compile --fqbn $FQBN lichtwecker

# Port suchen, bis zu 20 s warten (Maske wacht nach RESET kurz auf)
PORT=""
for i in {1..20}; do
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)
  [[ -n "$PORT" ]] && break
  (( i == 1 )) && echo "Warte auf die Maske ... jetzt RESET an der Maske drücken."
  sleep 1
done
if [[ -z "$PORT" ]]; then
  echo "Keine Maske gefunden. USB-C-Kabel (mit Daten) prüfen, RESET drücken und nochmal starten."
  echo "Hilft das nicht: BOOT gedrückt halten, RESET drücken, loslassen (Download-Modus), dann nochmal."
  exit 1
fi

echo "Lade auf $PORT ..."
arduino-cli upload --fqbn $FQBN -p "$PORT" lichtwecker
echo "Fertig. Einmal RESET drücken, dann startet die neue Firmware."
