# Schlafmaske-Lichtwecker

Eine Schlafmaske mit eingebauten LEDs, die vor der Weckzeit einen Sonnenaufgang simuliert und so sanft weckt.
Die Steuerung übernimmt ein ESP32-C3 mit einem NeoPixel-Streifen (WS2812). Die Uhrzeit kommt per WLAN über NTP. Zwischen den Weckzeiten schläft der Controller im Deep Sleep.

Die Produktidee, eine Bewertung und weitere Ideen stehen in [concept.md](concept.md).

## Stand

**Status: Prototyp. v1 lief auf Hardware; v1.1 (Bugfixes) ist kompiliert, aber noch nicht auf Hardware getestet.**

Der aktuelle Sketch liegt in [`lichtwecker/`](lichtwecker/).

| Version | Stand |
|---|---|
| `v1.0` (Git-Tag) | Originalstand vom 20.08.2026, nur die WLAN-Daten sind in `secrets.h` ausgelagert |
| `main` | v1.1: Bugfixes aus dem Code-Review, siehe [Änderungen in v1.1](#änderungen-in-v11) |

### Entwicklungsgeschichte

v1 entstand mit Gemini. Laut dem Gemini-Chat lief das so:
1. **Basisversion:** WLAN/NTP-Sync, RTC-Fallback, 4-Phasen-Sonnenaufgang, Deep Sleep bis 5 min vor der Weckzeit.
2. **Korrekturen:**
   - 3 s Upload-Delay, weil der sofortige Deep Sleep die USB-Uploads blockierte.
   - Weckzeit-Logik auf Unix-Timestamps (`mktime`) umgestellt.
   - 50 ms Tastenentprellung.
3. **Versuch, per BOOT-Taste aus dem Deep Sleep zu wecken.** Auf der Hardware kam der Fehler `sleep: gpio 9 is an invalid deep sleep wakeup IO`, der Versuch wurde verworfen.

**Auf Hardware getestet (v1):** NTP-Sync, RTC-Fallback, Timer-Weckung, Sonnenaufgang (echt und im Zeitraffer), Abbruch per Taste, Upload dank Delay. Messwerte zu Strom, Akku oder Temperatur gibt es nicht.

Der letzte Stand im Gemini-Chat ist nicht in Git. Er unterscheidet sich von `v1.0` nur in drei Punkten: Die Tastenweckung fehlt, es war eine Testkonfiguration aktiv (Weckzeit 19:46, `TEST_MODE = true`), und ein anderes WLAN war eingetragen. Diese Punkte sind in v1.1 abgedeckt.

Früher lagen zwei Ordner nebeneinander, `version1` (20.08.) und `version1_1` (25.08.). Beide waren byte-identisch, weil `version1_1` nur als Kopie für eine geplante v1.1 angelegt wurde. Sie wurden zu `lichtwecker/` zusammengeführt. Versionen werden jetzt über Git-Tags markiert, nicht über kopierte Ordner.

## Funktionsweise

```
Einschalten / Aufwachen
  └─ nur nach Reset: 3 s warten (Upload-Fenster für USB)
  └─ Uhrzeit holen: WLAN + NTP ─(Fehler)─> interne RTC-Zeit ─(ungültig)─> 60 s schlafen, neu versuchen
  └─ steht der Sonnenaufgang in ≤ 2 min an (oder hat vor ≤ 10 min begonnen)?
        ja   ─> bis zum Start warten ─> Sonnenaufgang (bei Verspätung passend einsteigen)
        nein ─> nichts tun
  └─ Deep Sleep bis 2 min vor dem nächsten Sonnenaufgang
```

**Ablauf des Weckers** mit den Standardwerten (Weckzeit 05:30):

| Phase | Zeit | Licht |
|---|---|---|
| 1 | 05:00–05:15 | Rot → Orange |
| 2 | 05:15–05:25 | Orange → Pfirsich |
| 3 | 05:25–05:30 | → Weiß |
| 4 | 05:30–06:00 | Weckzeit erreicht: Weiß pulsierend |
| – | ab 06:00 | aus, zurück in den Deep Sleep |

In den Phasen 1 bis 3 steigt die Helligkeit gleichmäßig und gammakorrigiert von 0 auf `MAX_BRIGHTNESS`.
**Taste während des Weckers:** Der Wecker wird beendet. **Taste in den 2 min davor:** Der Wecker wird für heute übersprungen.

Im `TEST_MODE` gilt 1 s = 1 min, und das Gerät schläft jeweils nur 10 s.

## Hardware

| Bauteil | Details |
|---|---|
| Mikrocontroller | **Seeed Studio XIAO ESP32C3** |
| LEDs | 32× NeoPixel (GRB, 800 kHz, genauer Typ undokumentiert), Daten an **GPIO 2** (XIAO-Pin D0) |
| Taster | BOOT-Taster des XIAO an **GPIO 9** (aktiv LOW, interner Pull-up) |
| LED-Abschaltung | optional: MOSFET/Load-Switch an `LED_POWER_PIN` (Standard: keiner) |
| Stromversorgung | bisher USB-C; Akku noch nicht festgelegt |

GPIO 2 ist ein Strapping-Pin. Zwischendurch wurde deshalb GPIO 4 getestet. Mit dem vorhandenen Aufbau gab es an GPIO 2 aber keine Boot-Probleme, darum wurde zurückgewechselt.

Die maximale Helligkeit ist per Software auf 76/255 (≈ 30 %) begrenzt. Bei voll weißem Licht ziehen 32 LEDs trotzdem noch ungefähr **0,5–0,6 A**.

> **Wichtig für den Akkubetrieb:**
> - **Die BOOT-Taste (GPIO 9) kann den Chip nicht aus dem Deep Sleep wecken**, das geht beim ESP32-C3 nur an GPIO 0–5. Während der Wecker läuft, funktioniert sie trotzdem. Wer per Taste aufwecken will, schließt einen Taster an GPIO 0–5 an und stellt `BUTTON_PIN` um.
> - **Ohne `LED_POWER_PIN` ziehen die LEDs auch im Deep Sleep Strom**, je nach Typ ~0,5–1 mA pro LED, zusammen ~15–30 mA. Ein kleiner Akku ist damit in wenigen Tagen leer. Abhilfe: LED-Versorgung über einen MOSFET schalten und dessen Gate an `LED_POWER_PIN` legen.

## Einrichtung

### Benötigte Tools

- **Arduino IDE 2.x** *oder* `arduino-cli` (beide installiert) *oder* PlatformIO in VS Code
- **ESP32-Board-Paket von Espressif** (installiert)
  In der IDE unter Einstellungen → „Zusätzliche Boardverwalter-URLs“:
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, dann im Boardverwalter „esp32“ installieren.
- **Bibliothek „Adafruit NeoPixel“** (installiert)

### Zugangsdaten

```sh
cp lichtwecker/secrets.example.h lichtwecker/secrets.h
# WLAN-Name und -Passwort in secrets.h eintragen
```

`secrets.h` steht in `.gitignore` und wird nie committet.

### Kompilieren & Flashen

In der Arduino IDE `lichtwecker/lichtwecker.ino` öffnen und dann einstellen:
- Board: **XIAO_ESP32C3** (Port z. B. `/dev/cu.usbmodem14101`)
- **USB CDC On Boot: Enabled**. Ohne diese Einstellung gibt es keine serielle Ausgabe über den USB-Port des C3.

Oder per CLI:

```sh
brew install arduino-cli
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit NeoPixel"

FQBN="esp32:esp32:XIAO_ESP32C3"
arduino-cli compile --fqbn $FQBN lichtwecker
arduino-cli upload  --fqbn $FQBN -p /dev/cu.usbmodem* lichtwecker
arduino-cli monitor -p /dev/cu.usbmodem* -c baudrate=115200
```

> **Tipp:** Der Controller ist die meiste Zeit im Deep Sleep, dann ist kein USB-Upload möglich. Zum Flashen **RESET drücken**: Danach wartet der Sketch 3 s. Klappt das nicht, BOOT gedrückt halten, RESET drücken und so den Download-Modus erzwingen.

### Konfiguration

Alle Einstellungen stehen oben in `lichtwecker.ino`:

| Konstante | Standard | Bedeutung |
|---|---|---|
| `ALARM_HOUR` / `ALARM_MINUTE` | 5 / 30 | Weckzeit, zu der der Sonnenaufgang fertig ist |
| `SUNRISE_MINUTES` | 30 | Dauer des Sonnenaufgangs vor der Weckzeit |
| `DAYLIGHT_MINUTES` | 30 | Dauer des pulsierenden Lichts nach der Weckzeit |
| `MAX_BRIGHTNESS` | 76 | Obergrenze pro Farbkanal (0–255) |
| `WAKE_MARGIN_SEC` | 120 | so viel früher aufwachen, für WLAN/NTP |
| `LATE_GRACE_SEC` | 600 | bis zu dieser Verspätung wird noch geweckt |
| `LED_PIN`, `BUTTON_PIN`, `LED_POWER_PIN`, `NUM_LEDS` | | Hardware |
| `TIME_ZONE`, `NTP_SERVER` | Berlin, `de.pool.ntp.org` | Zeit |
| `TEST_MODE` | `false` | Zeitraffer zum Testen |

Jede Änderung erfordert bisher ein erneutes Flashen.

## Änderungen in v1.1

Behoben aus dem Code-Review von v1:

| Problem in v1 | Lösung in v1.1 |
|---|---|
| Der Sonnenaufgang begann erst **zur** Weckzeit, volle Helligkeit kam 30 min zu spät | Der Sonnenaufgang startet `SUNRISE_MINUTES` vor der Weckzeit und ist zur Weckzeit fertig |
| Ohne WLAN fehlte die Zeitzone, der Wecker ging 1–2 h falsch | `TZ` wird bei jedem Start gesetzt |
| Der RTC-Fallback rechnete Zeit selbst hoch und ignorierte dabei Wachzeit und Tastenweckung | Die Systemzeit läuft im Deep Sleep ohnehin weiter; sie wird direkt verwendet |
| Der „NTP-Sync“ nach dem Deep Sleep korrigierte nie etwas: `getLocalTime()` meldete sofort Erfolg, weil die Zeit schon gültig war | Es wird auf die echte NTP-Antwort gewartet (Sync-Callback) |
| Weckzeiten um Mitternacht (00:00–00:04) funktionierten nicht | Die Berechnung erfolgt über echte Zeitpunkte statt „heute“; getestet für Mitternacht und beide Zeitumstellungen |
| Die Warteschleife vor dem Alarm konnte endlos hängen und ignorierte die Taste | Die Wartezeit ist auf 2 min begrenzt, die Taste überspringt den Wecker |
| Weckte der Chip verspätet auf, begann der Sonnenaufgang von vorn oder fiel aus | Bis 10 min Verspätung steigt der Wecker an der passenden Stelle ein |
| Lineare Helligkeitsrampe mit `setBrightness()` (grob, verliert Farbe) | Gammakorrigierte Rampe, Farben werden direkt skaliert |
| Tastenweckung an GPIO 9 erzeugte einen Fehler (auf Hardware bestätigt) | Tastenweckung wird nur an gültigen Pins (GPIO 0–5) aktiviert |
| Keine Möglichkeit, die LEDs im Schlaf stromlos zu schalten | optionaler `LED_POWER_PIN` |
| 3 s `delay` bei jedem Aufwachen | nur noch nach Reset |

## Offene Punkte

- **Nicht auf Hardware getestet.** v1.1 kompiliert, und die Zeitplan-Logik ist auf dem Mac getestet. Tastenweckung, `LED_POWER_PIN` und das Verhalten im Deep Sleep müssen auf dem Gerät geprüft werden, am besten erst mit `TEST_MODE = true`.
- **Taste:** Die Hardware-Einschränkung an GPIO 9 bleibt (siehe [Hardware](#hardware)). Für eine Weckung per Taste an GPIO 0–5 aktiviert die ESP-IDF den internen Pull-up im Schlaf automatisch. Espressif empfiehlt beim Wecken auf LOW trotzdem einen externen Pull-up.
- **Stufen am Anfang:** Bei sehr niedriger Helligkeit sind die 8-Bit-Stufen der WS2812 sichtbar, das ist hardwarebedingt. Abhilfe: zeitliches Dithering oder LEDs mit höherer Auflösung.
- **Weiß pulsierend** in Phase 4 hat vollen Blauanteil, direkt vor den Augen.
- GPIO 2 ist auf dem C3 ein Strapping-Pin (meist unkritisch).

## Roadmap / Ideen

**Kurzfristig**
- v1.1 auf der Hardware testen und dann als `v1.1` taggen
- Auf PlatformIO umsteigen (`platformio.ini` mit festen Versionen von Core und Library), damit Builds reproduzierbar sind
- Batteriespannung über ADC messen und bei niedrigem Stand warnen (z. B. kurzes rotes Blinken)

**Konfiguration**
- Weckzeit ohne Neu-Flashen setzen: WiFiManager-Captive-Portal oder BLE, Speicherung in `Preferences` (NVS)
- Später eine kleine Web-App/PWA mit Web Bluetooth, Wochentagsprofile

**Licht**
- Physikalisch plausibler Farbverlauf (Farbtemperatur ~1800 K → 4000 K)
- Blauanteil begrenzen, weil die LEDs direkt vor den Augen sitzen
- Snooze per Taste statt sofort beenden; sanftes Ausblenden statt Pulsieren

**Hardware**
- Taster an GPIO 0–5, MOSFET für die LED-Versorgung
- Weniger und diffusere LEDs (Hitze, Blendung, Stromverbrauch)
- LiPo mit Schutzschaltung, USB-C-Laden
- Vibrationsmotor als Backup-Alarm
- Später: Beschleunigungssensor für „Smart Wake“ im leichten Schlaf

## Projektstruktur

```
.
├── README.md
├── concept.md              Produktidee, Bewertung, Ideen
├── .gitignore
└── lichtwecker/
    ├── lichtwecker.ino     aktueller Sketch
    ├── secrets.example.h   Vorlage für Zugangsdaten
    └── secrets.h           lokal, nicht versioniert
```
