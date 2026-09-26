# Schlafmaske-Lichtwecker

Eine Schlafmaske mit eingebauten LEDs, die zur Weckzeit einen Sonnenaufgang simuliert und so sanft weckt.
Die Steuerung übernimmt ein ESP32-C3 mit einem NeoPixel-Streifen (WS2812). Die Uhrzeit kommt per WLAN über NTP. Zwischen den Weckzeiten schläft der Controller im Deep Sleep.

Die Produktidee, eine Bewertung und weitere Ideen stehen in [concept.md](concept.md).

## Stand

**Status: funktionsfähiger Prototyp (v1), zuletzt bearbeitet im August 2026.**

Der aktuelle Sketch liegt in [`lichtwecker/`](lichtwecker/). Der Git-Tag `v1.0` markiert den Originalstand vom 20.08.2026, nur die WLAN-Daten sind in `secrets.h` ausgelagert.

Früher lagen zwei Ordner nebeneinander, `version1` (20.08.) und `version1_1` (25.08.). Beide waren byte-identisch, weil `version1_1` nur als Kopie für eine geplante v1.1 angelegt wurde. Sie wurden zu `lichtwecker/` zusammengeführt. Versionen werden künftig über Git-Tags markiert (`git tag v1.1`), nicht über kopierte Ordner.

## Funktionsweise

```
Einschalten / Aufwachen
  └─ 3 s warten (Upload-Fenster für USB)
  └─ Uhrzeit holen: WLAN + NTP ─(Fehler)─> RTC-Fallback ─(Fehler)─> 60 s schlafen, neu versuchen
  └─ liegt jetzt im Fenster Weckzeit −5 min … +5 min?
        ja   ─> bis zur Weckzeit warten ─> Sonnenaufgang abspielen
        nein ─> nichts tun
  └─ Deep Sleep bis 5 min vor der nächsten Weckzeit (Timer; zusätzlich Weck-Taste)
```

**Sonnenaufgang** (ab der Weckzeit, per Taste abbrechbar):

| Phase | Zeit | Licht |
|---|---|---|
| 1 | 0–15 min | Rot → Orange, Helligkeit 3 → 40 % von `MAX_BRIGHTNESS` |
| 2 | 15–25 min | Orange → Pfirsich, 40 → 80 % |
| 3 | 25–30 min | → Warmweiß/Weiß, 80 → 100 % |
| 4 | 30–60 min | Weiß pulsierend |
| – | ab 60 min | aus, zurück in den Deep Sleep |

Im `TEST_MODE` gilt 1 s = 1 min, und das Gerät schläft jeweils nur 10 s.

## Hardware

| Bauteil | Details |
|---|---|
| Mikrocontroller | ESP32-C3 (z. B. ESP32-C3 SuperMini / DevKitM-1) |
| LEDs | 32× WS2812/NeoPixel (GRB, 800 kHz), Daten an **GPIO 2** |
| Taster | BOOT-Taster an **GPIO 9** (aktiv LOW, interner Pull-up) |
| Stromversorgung | nicht dokumentiert, vermutlich LiPo oder USB |

Die maximale Helligkeit ist per Software auf 76/255 (≈ 30 %) begrenzt. Bei voll weißem Licht ziehen 32 LEDs trotzdem noch ungefähr **0,5–0,6 A**.

## Einrichtung

### Benötigte Tools

- **Arduino IDE 2.x** (installiert) *oder* `arduino-cli` *oder* PlatformIO in VS Code
- **ESP32-Board-Paket von Espressif**. *Fehlt auf diesem Mac noch* (installiert ist bisher nur AVR).
  Unter Einstellungen → „Zusätzliche Boardverwalter-URLs“ eintragen:
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, dann im Boardverwalter „esp32“ installieren.
- **Bibliothek „Adafruit NeoPixel“**. *Fehlt auf diesem Mac noch.* Installation über den Bibliotheksverwalter.

### Zugangsdaten

```sh
cp lichtwecker/secrets.example.h lichtwecker/secrets.h
# WLAN-Name und -Passwort in secrets.h eintragen
```

`secrets.h` steht in `.gitignore` und wird nie committet.

### Kompilieren & Flashen

In der Arduino IDE `lichtwecker/lichtwecker.ino` öffnen und dann einstellen:
- Board: **ESP32C3 Dev Module**
- **USB CDC On Boot: Enabled**. Ohne diese Einstellung gibt es keine serielle Ausgabe über den USB-Port des C3.

Oder per CLI:

```sh
brew install arduino-cli
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"

FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc"
arduino-cli compile --fqbn $FQBN lichtwecker
arduino-cli upload  --fqbn $FQBN -p /dev/cu.usbmodem* lichtwecker
arduino-cli monitor -p /dev/cu.usbmodem* -c baudrate=115200
```

> **Tipp:** Weil der Controller die meiste Zeit im Deep Sleep ist, klappt ein Upload nur in den ersten 3 s nach dem Aufwachen. Alternativ BOOT gedrückt halten, RESET drücken und so den Download-Modus erzwingen.

### Konfiguration

Alle Einstellungen stehen oben in `lichtwecker.ino`: `ALARM_HOUR`/`ALARM_MINUTE`, `MAX_BRIGHTNESS`, `NUM_LEDS`, Pins, Zeitzone, NTP-Server und `TEST_MODE`.
Jede Änderung erfordert bisher ein erneutes Flashen.

## Bekannte Probleme

Aus dem Code-Review. Nicht auf Hardware getestet, nach Schwere sortiert:

1. **Die Weck-Taste weckt vermutlich nicht aus dem Deep Sleep.** Auf dem ESP32-C3 können nur GPIO 0–5 aus dem Deep Sleep wecken. `esp_deep_sleep_enable_gpio_wakeup(1ULL << 9, …)` schlägt deshalb fehl, und der Rückgabewert wird nicht geprüft. Lösung: Taster an GPIO 0–5 legen und den Rückgabewert prüfen.
2. **Der Sonnenaufgang beginnt erst *zur* Weckzeit.** Volle Helligkeit ist erst 30 min danach erreicht. Bei einem Lichtwecker sollte der Sonnenaufgang *zur* Weckzeit fertig sein, also 20–30 min vorher starten.
3. **Die LEDs ziehen im Deep Sleep weiter Strom.** Auch „ausgeschaltete“ WS2812 verbrauchen je nach Typ ungefähr 0,5–1 mA pro LED. Bei 32 LEDs sind das ~15–30 mA dauerhaft, damit ist ein kleiner Akku in wenigen Tagen leer. Lösung: LED-Versorgung per MOSFET bzw. Load-Switch abschalten.
4. **Der RTC-Fallback liefert eine falsche Uhrzeit.** Wenn WLAN fehlschlägt:
   - Die Zeitzone wird nie gesetzt, weil `configTzTime` nur nach erfolgreichem WLAN läuft. `localtime_r` rechnet dann in UTC, und der Wecker geht 1–2 h falsch.
   - `rtcLastEpoch += rtcLastSleepDurationSec` ignoriert die Wachzeit (bis zu 60 min Weckphase) und vorzeitiges Aufwachen per Taste.

   Dabei läuft die Systemzeit des ESP32 im Deep Sleep ohnehin weiter. Einfacher und korrekt ist `time(nullptr)` zusammen mit `setenv("TZ", …); tzset();` bei jedem Start.
5. **Die Warteschleife vor dem Alarm kann hängen.** `while (… ) { delay(5000); getLocalTime(&timeinfo); }` hat kein Timeout. Die Taste wird dort nicht abgefragt, und wenn `getLocalTime` fehlschlägt, bleibt `timeinfo` unverändert.
6. **Weckzeiten kurz nach Mitternacht funktionieren nicht.** Bei 00:00–00:04 liegt der Pre-Alarm am Vortag, und die Fensterberechnung bezieht sich nur auf „heute“.
7. **Die Helligkeit steigt in sichtbaren Stufen.** Die Rampe ist linear, es gibt keine Gamma-Korrektur, und der Start bei Helligkeit 3 ist grob abgestuft. Außerdem kann `setBrightness()` laut Adafruit bei niedrigen Werten Farbinformationen verlieren. Besser ist es, die Farben direkt zu skalieren (`strip.gamma32()`).
8. Kleinere Punkte:
   - Die Konfiguration ist fest einkompiliert.
   - `TEST_MODE` erfordert Neu-Flashen.
   - Kein Snooze.
   - Bei jedem Aufwachen gibt es 3 s `delay` plus WLAN-Verbindung (bis zu 25 s), was Energie kostet.
   - GPIO 2 ist auf dem C3 ein Strapping-Pin (meist unkritisch).

## Roadmap / Ideen

**Kurzfristig (Bugs & Basis)**
- Punkte 1–6 aus [Bekannte Probleme](#bekannte-probleme) beheben, vor allem die Tasten-Pin und den Sonnenaufgangs-Start
- Auf PlatformIO umsteigen (`platformio.ini` mit festen Versionen von Core und Library), damit Builds reproduzierbar sind
- Batteriespannung über ADC messen und bei niedrigem Stand warnen (z. B. kurzes rotes Blinken)

**Konfiguration**
- Weckzeit ohne Neu-Flashen setzen: WiFiManager-Captive-Portal oder BLE, Speicherung in `Preferences` (NVS)
- Später eine kleine Web-App/PWA mit Web Bluetooth, Wochentagsprofile

**Licht**
- Physikalisch plausibler Farbverlauf (Farbtemperatur ~1800 K → 4000 K), gammakorrigiert, über 20–30 min
- Blauanteil begrenzen, weil die LEDs direkt vor den Augen sitzen
- Sanftes Ausschalten statt Pulsieren und/oder Snooze per Taste

**Hardware**
- Weniger und diffusere LEDs (Hitze, Blendung, Stromverbrauch)
- LiPo mit Schutzschaltung, USB-C-Laden, LED-Versorgung abschaltbar
- Vibrationsmotor als Backup-Alarm
- Später: Beschleunigungssensor für „Smart Wake“ im leichten Schlaf

## Projektstruktur

```
.
├── README.md
├── concept.md              Produktidee, Bewertung, Ideen
├── .gitignore
├── lichtwecker/
│   ├── lichtwecker.ino     aktueller Sketch
│   ├── secrets.example.h   Vorlage für Zugangsdaten
│   └── secrets.h           lokal, nicht versioniert
```
