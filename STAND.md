# Stand: Schlafmaske-Lichtwecker (2026-11-28)

## Was funktioniert
- Firmware kompiliert fehlerfrei und ohne Warnungen (`arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --warnings all lichtwecker`, 85 % Flash).
- Sonnenaufgang vor der Weckzeit, NTP/RTC-Fallback, Deep Sleep, Tastenabbruch, Einstellungsseite, Weckzeit an der Maske (v1/v1.1 liefen auf Hardware).
- Reine Logik liegt in `lichtwecker/lichtwecker_logic.h` und ist per `tests/run.sh` abgesichert (289 Prüfungen, nur clang++, ca. 1 s): Weckzeit über Mitternacht, Sommerzeit-Wechsel, Sonnenaufgang länger als die verbleibende Zeit (Einstieg mitten drin), Gamma/Flackern, Menü, Statusanzeige.
- Leitstand-Task `lichtwecker-tests` startet die Tests per Knopf.

## Behoben in dieser Runde (mit Test belegt, noch nicht auf der Maske geprüft)
- Sommerzeit: Lag die Weckzeit in der übersprungenen Stunde (z. B. 03:00 am Umstellungstag im März), wurde sie um eine Stunde zu spät berechnet (04:00). Die Weckzeit wird jetzt direkt als Ortszeit berechnet, der Sonnenaufgang beginnt genau seine Dauer davor.
- Pulsieren nach der Weckzeit: Es startete an beliebiger Stelle der Sinuskurve, das Licht fiel an der Weckzeit zufällig auf 32 bis 100 % ab. Jetzt beginnt das Pulsieren am Höchstwert, der Übergang ist nahtlos.
- Gamma am unteren Ende: Das Mindest-Glimmen ging immer auf Rot, bei blauen/grünen Farben sprang es dadurch von Rot auf Blau. Jetzt glimmt der stärkste Kanal der Farbe (Sonnenaufgang unverändert, da Rot dominiert).

## Nächste Schritte
1. Firmware auf die Maske flashen (`flash.sh`, Leitstand „Lichtwecker flashen“) und prüfen: Abend-Check, Sonnenaufgang (Zeitraffer, Menü), nahtloser Übergang ins Pulsieren, Probelauf mit echtem Deep Sleep.
2. Drift-Test auswerten (`DRIFT_CHECK_ENABLED = false`, Messwerte in der Einstellungsseite), danach entscheiden, ob der Nacht-Abgleich wieder an muss.
3. Optional: Tasten-Entprellung/-Erkennung (`waitForButton`) in eine testbare Zustandsmaschine mit simulierter Zeit überführen. Aktuell ist nur die Klassifizierung kurz/lang ausgelagert.

## Offene Fragen an Philipp
- Soll das Licht nach der Weckzeit weiter hell pulsieren (60 bis 100 %) oder sanfter/dunkler werden?
- Akku-Umbau (MOSFET, Akkuüberwachung) ist laut dir zurückgestellt, du lädst täglich per USB-C. Bleibt das so? Plan: `docs/akku-und-ausbauplan.md`.
- Soll der Nacht-Abgleich der Uhr nach dem Drift-Test fest aktiviert werden?

## Hinweise
- Flash-Test auf der Maske steht aus (in dieser Runde keine Hardware angeschlossen, nicht geflasht).
- `lichtwecker/secrets.h` (WLAN-Zugangsdaten) ist per `.gitignore` ausgeschlossen, nie committen.
