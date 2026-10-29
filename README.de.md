# Schlafmaske-Lichtwecker

Eine Schlafmaske mit eingebauten LEDs, die vor der Weckzeit einen Sonnenaufgang simuliert und so sanft weckt.
Die Steuerung übernimmt ein ESP32-C3 mit zwei NeoPixel-Ringen (je 8 LEDs, einer pro Auge). Die Uhrzeit kommt per WLAN über NTP. Zwischen den Weckzeiten schläft der Controller im Deep Sleep.

**Bedienung: [BEDIENUNGSANLEITUNG.md](BEDIENUNGSANLEITUNG.md)**. Die Produktidee, eine Bewertung und weitere Ideen stehen in [concept.md](concept.md).

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
  └─ nur nach Reset: 3 s Fenster (erste LED blau) für USB-Upload und Testmenü
  └─ Uhrzeit holen: WLAN + NTP ─(Fehler)─> interne RTC-Zeit ─(ungültig)─> 60 s schlafen, neu versuchen
  └─ nur nach Reset: Statusanzeige (siehe Testen)
  └─ steht der Sonnenaufgang in ≤ 2 min an (oder hat vor ≤ 30 min begonnen)?
        ja   ─> bis zum Start warten ─> Sonnenaufgang (bei Verspätung passend einsteigen)
        nein ─> nichts tun
  └─ Deep Sleep bis 2 min vor dem nächsten Sonnenaufgang
     (optional mit Zwischenstopp zum Nachstellen der Uhr, zurzeit für den Drift-Test abgeschaltet)
```

**Ablauf des Weckers** mit den Standardwerten (Weckzeit 08:00):

| Phase | Zeit | Licht |
|---|---|---|
| 1 | 07:30–07:45 | Rot → Orange |
| 2 | 07:45–07:55 | Orange → Pfirsich |
| 3 | 07:55–08:00 | → Weiß |
| 4 | 08:00–08:30 | Weckzeit erreicht: Weiß pulsierend |
| – | ab 08:30 | aus, zurück in den Deep Sleep |

In den Phasen 1 bis 3 steigt die Helligkeit gleichmäßig und gammakorrigiert von 0 auf die eingestellte Maximalhelligkeit.
**Taste während des Weckers:** Der Wecker wird beendet. **Taste in den 2 min davor:** Der Wecker wird für heute übersprungen.

Zum Testen, ohne auf den Morgen zu warten, gibt es das [Testmenü](#testen).

## Hardware

| Bauteil | Details |
|---|---|
| Mikrocontroller | **Seeed Studio XIAO ESP32C3** |
| LEDs | 2× Joycabin WS2812-Ring (8 LEDs, 5050) = 16 LEDs, hintereinander geschaltet, **Plus am 3V3-Pin**, Daten an **GPIO 2** (XIAO-Pin D0) |
| Taster | BOOT-Taster des XIAO an **GPIO 9** (aktiv LOW, interner Pull-up) |
| LED-Abschaltung | optional: MOSFET/Load-Switch an `LED_POWER_PIN` (Standard: keiner) |
| Akku | LiPo 500 mAh an den BAT-Pads des XIAO, Laden über USB-C |

GPIO 2 ist ein Strapping-Pin. Zwischendurch wurde deshalb GPIO 4 getestet. Mit dem vorhandenen Aufbau gab es an GPIO 2 aber keine Boot-Probleme, darum wurde zurückgewechselt.

Die maximale Helligkeit ist per Software auf 76/255 (≈ 30 %) begrenzt. Bei voll weißem Licht ziehen die 16 LEDs trotzdem noch ungefähr **0,3 A**.

> **Wichtig für den Akkubetrieb:** Mit dem jetzigen Aufbau hält der Akku nur **~1–1,5 Tage**, also **täglich laden**. Rechnung, Messmöglichkeiten und Umbau (alles mit Teilen aus dem Elegoo-Kit) stehen in [docs/akku-und-ausbauplan.md](docs/akku-und-ausbauplan.md).
>
> - **Die BOOT-Taste (GPIO 9) kann den Chip nicht aus dem Deep Sleep wecken**, das geht beim ESP32-C3 nur an GPIO 0–5. Während der Wecker läuft, funktioniert sie trotzdem. Wer per Taste aufwecken will, schließt einen Taster an GPIO 0–5 an und stellt `BUTTON_PIN` um.
> - **Ohne `LED_POWER_PIN` ziehen die LEDs auch im Deep Sleep Strom**, je nach Typ ~0,5–1 mA pro LED, zusammen ~8–16 mA. Ein kleiner Akku ist damit in wenigen Tagen leer. Abhilfe: LED-Versorgung über einen MOSFET schalten und dessen Gate an `LED_POWER_PIN` legen.

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
# Namen und Passwörter der WLANs in secrets.h eintragen (WLAN 1 = Standard, WLAN 2 = Ausweich)
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

> **Tipp:** Der Controller ist die meiste Zeit im Deep Sleep, dann ist kein USB-Upload möglich. Zum Flashen **RESET drücken**: Danach ist das Board 3 s wach (erste LED blau), plus die Zeit für den WLAN-Sync. Klappt das nicht, BOOT gedrückt halten, RESET drücken und so den Download-Modus erzwingen.

### Konfiguration

**Weckzeit, Wecker an/aus, Sonnenaufgangsdauer, Lichtdauer, Helligkeit und bevorzugtes WLAN** stellt man ohne Neu-Flashen über die Einstellungsseite ein (Menü-Modus 4, [Bedienungsanleitung](BEDIENUNGSANLEITUNG.md#weckzeit-ändern-am-handy)). Gespeichert wird im Flash (`Preferences`, Namespace `wecker`). Das übersteht Reset, leeren Akku und neues Hochladen.

Oben in `lichtwecker.ino` stehen nur noch Standardwerte und Hardware-Konstanten:

| Konstante | Standard | Bedeutung |
|---|---|---|
| `DEFAULT_ALARM_HOUR` / `DEFAULT_ALARM_MINUTE` | 8 / 0 | Weckzeit, solange nichts gespeichert ist. Einmalige Umstellung älterer Geräte von 05:30 auf 08:00 über `SETTINGS_VERSION` |
| `DEFAULT_SUNRISE_MINUTES` / `DEFAULT_DAYLIGHT_MINUTES` | 30 / 30 | Sonnenaufgang vor, pulsierendes Licht nach der Weckzeit |
| `DEFAULT_MAX_BRIGHTNESS` | 76 | Obergrenze pro Farbkanal (0–255) |
| `WAKE_MARGIN_SEC` | 120 | so viel früher aufwachen, für WLAN/NTP |
| `DRIFT_CHECK_ENABLED` | **`false`** (Test) | Nacht-Abgleich: längeren Schlaf (> `DRIFT_CHECK_MIN_SEC`) teilen und zwischendurch die Uhr nachstellen. Siehe [Drift-Test](#drift-test-läuft) |
| `DRIFT_LOG_MIN_SEC` / `DRIFT_LOG_SIZE` | 1800 / 30 | Drift-Protokoll: nur Abgleiche nach ≥ 30 min, max. 30 Einträge |
| `WEB_TIMEOUT_MS` | 5 min | so lange bleibt die Einstellungsseite ohne Aufruf online |
| `LED_PIN`, `BUTTON_PIN`, `LED_POWER_PIN`, `NUM_LEDS`, `LEDS_PER_RING` | | Hardware |
| `TIME_ZONE`, `NTP_SERVER`, `HOSTNAME`, `AP_SSID` | Berlin, `de.pool.ntp.org`, `lichtwecker`, `Lichtwecker` | Zeit und Netzwerk |

Verspätetes Aufwachen wird bis zur Dauer des Sonnenaufgangs abgefangen: Der Wecker steigt dann an der passenden Stelle ein.

## Bedienung & Testen

**Die Bedienung für den Alltag steht in der [Bedienungsanleitung](BEDIENUNGSANLEITUNG.md):** Abend-Check, LED-Signale, Wecker ausschalten, Menü, Einstellungsseite, Laden.

Kurzfassung:
- **RESET** = Abend-Check. Die Ringe füllen sich grün (WLAN), dann zeigen orange LEDs die Stunden und blaue die 10-Minuten bis zum Wecken.
- **BOOT während des Weckers** = aus.
- **RESET, dann BOOT** = Menü. Modi:
  1. LED-Test
  2. Zeitraffer-Vorschau
  3. Probelauf mit echtem Deep Sleep
  4. Einstellungen (eigenes WLAN „Lichtwecker“, Seite öffnet sich am iPhone von selbst; alternativ `http://lichtwecker.local`)
  5. Wecker an/aus
  6. WLAN wechseln

**Empfohlen vor der ersten Nacht:** Modus 1 und Modus 3 **im Akkubetrieb** durchlaufen lassen. Nur so zeigt sich, ob die LEDs auch ohne USB genug Strom bekommen.

## Drift-Test (läuft)

**Seit 29.09.2026** ist der Nacht-Abgleich (`DRIFT_CHECK_ENABLED = false`) abgeschaltet. So lässt sich messen, wie stark die interne Uhr des XIAO im Deep Sleep wirklich abweicht.

- **Messung:** Bei jedem NTP-Abgleich vergleicht `recordClockDrift()` die interne Uhr mit der NTP-Zeit, auf Millisekunden genau. War der letzte Abgleich mindestens 30 min her, wird das Ergebnis im Flash gespeichert (Namespace `drift`, Ringpuffer mit 30 Einträgen). Die wichtige Messung ist die morgens: Abend-Check per RESET → Nacht → Aufwachen vor dem Sonnenaufgang.
- **Auslesen:** Einstellungsseite (Menü-Modus 4) → „Uhr-Abweichung (Test)“, oder die serielle Ausgabe nach RESET (`[DRIFT] …`).
- **Auswertung:** Maßgeblich ist die Spalte „pro 8 h“. Plus heißt: Die Uhr ging nach, der Wecker kam zu spät.
  - Unter ~±60 s pro Nacht: Der Nacht-Abgleich ist unnötig und kostet nur Akku.
  - Deutlich mehr, oder mehr als die Kulanz (Sonnenaufgangsdauer): `DRIFT_CHECK_ENABLED = true` setzen.
- **Wieder einschalten:** eine Zeile in `lichtwecker.ino`, der Code dafür ist unverändert vorhanden. Die Simulation für den eingeschalteten Zustand steht in [docs/akku-und-ausbauplan.md](docs/akku-und-ausbauplan.md) (Abschnitt 4).

## Änderungen in v1.1

Behoben aus dem Code-Review von v1:

| Problem in v1 | Lösung in v1.1 |
|---|---|
| Der Sonnenaufgang begann erst **zur** Weckzeit, volle Helligkeit kam 30 min zu spät | Der Sonnenaufgang startet vor der Weckzeit und ist zur Weckzeit fertig |
| Ohne WLAN fehlte die Zeitzone, der Wecker ging 1–2 h falsch | `TZ` wird bei jedem Start gesetzt |
| Der RTC-Fallback rechnete Zeit selbst hoch und ignorierte dabei Wachzeit und Tastenweckung | Die Systemzeit läuft im Deep Sleep ohnehin weiter; sie wird direkt verwendet |
| Der „NTP-Sync“ nach dem Deep Sleep korrigierte nie etwas: `getLocalTime()` meldete sofort Erfolg, weil die Zeit schon gültig war | Es wird auf die echte NTP-Antwort gewartet (Sync-Callback) |
| Weckzeiten um Mitternacht (00:00–00:04) funktionierten nicht | Die Berechnung erfolgt über echte Zeitpunkte statt „heute“; getestet für Mitternacht und beide Zeitumstellungen |
| Die Warteschleife vor dem Alarm konnte endlos hängen und ignorierte die Taste | Die Wartezeit ist auf 2 min begrenzt, die Taste überspringt den Wecker |
| Weckte der Chip verspätet auf, begann der Sonnenaufgang von vorn oder fiel aus | Bis 30 min Verspätung steigt der Wecker an der passenden Stelle ein |
| Lineare Helligkeitsrampe mit `setBrightness()` (grob, verliert Farbe) | Gammakorrigierte Rampe, Farben werden direkt skaliert |
| Tastenweckung an GPIO 9 erzeugte einen Fehler (auf Hardware bestätigt) | Tastenweckung wird nur an gültigen Pins (GPIO 0–5) aktiviert |
| Keine Möglichkeit, die LEDs im Schlaf stromlos zu schalten | optionaler `LED_POWER_PIN` |
| 3 s `delay` bei jedem Aufwachen | nur noch nach Reset |
| Ein langer Deep Sleep lief ohne Korrektur durch; der interne Takt driftet um einige Prozent, der Wecker kam dadurch zu spät oder fiel aus | Zwischenstopp nach 90 % des Schlafs zum Nachstellen der Uhr (**zurzeit für den Drift-Test abgeschaltet**); mehr Kulanz bei Verspätung (bis zur Sonnenaufgangsdauer) |
| Weckzeit nur durch Code-Änderung und Neu-Flashen einstellbar | Einstellungsseite (eigenes WLAN mit Captive Portal + `lichtwecker.local`), Wecker an/aus, nächsten Wecker aussetzen |
| Nur ein WLAN fest im Code | Zwei WLANs in `secrets.h`, Auswahl über Menü/Seite, automatischer Ausweich auf das andere |
| Testen nur über `TEST_MODE` und Neu-Flashen, sonst Warten bis zum Morgen | Statusanzeige nach jedem Reset und Testmenü per BOOT-Taste (LED-Test, Zeitraffer, Probelauf) |

## Offene Punkte

- **Nicht auf Hardware getestet.** v1.1 kompiliert, und die Zeitplan-Logik ist auf dem Mac getestet. Auf dem Gerät mit dem [Testmenü](#testen) prüfen, vor allem den Probelauf im Akkubetrieb.
- **Akkustand** lässt sich noch nicht messen. Der XIAO ESP32C3 hat dafür keinen eingebauten Spannungsteiler, nötig wären 2 Widerstände (z. B. 2× 220 kΩ) von BAT an einen freien Analog-Pin (z. B. A1/GPIO 3).
- **Taste:** Die Hardware-Einschränkung an GPIO 9 bleibt (siehe [Hardware](#hardware)). Für eine Weckung per Taste an GPIO 0–5 aktiviert die ESP-IDF den internen Pull-up im Schlaf automatisch. Espressif empfiehlt beim Wecken auf LOW trotzdem einen externen Pull-up.
- **Stufen am Anfang:** Bei sehr niedriger Helligkeit sind die 8-Bit-Stufen der WS2812 sichtbar, das ist hardwarebedingt. Abhilfe: zeitliches Dithering oder LEDs mit höherer Auflösung.
- **Weiß pulsierend** in Phase 4 hat vollen Blauanteil, direkt vor den Augen.
- GPIO 2 ist auf dem C3 ein Strapping-Pin (meist unkritisch).

## Roadmap / Ideen

**Kurzfristig**
- v1.1 mit dem Testmenü auf der Hardware testen und dann als `v1.1` taggen
- Auf PlatformIO umsteigen (`platformio.ini` mit festen Versionen von Core und Library), damit Builds reproduzierbar sind
- Akkuspannung über Spannungsteiler + ADC messen, in der Statusanzeige zeigen und bei niedrigem Stand warnen

Details und Reihenfolge: [docs/akku-und-ausbauplan.md](docs/akku-und-ausbauplan.md)

**Konfiguration**
- ~~Weckzeit ohne Neu-Flashen setzen~~ (erledigt: Einstellungsseite im WLAN)
- Wochentagsprofile und „Morgen später“-Schnellauswahl auf der Einstellungsseite

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
├── BEDIENUNGSANLEITUNG.md      Bedienung im Alltag
├── concept.md              Produktidee, Bewertung, Ideen
├── .gitignore
├── docs/
│   └── akku-und-ausbauplan.md   Akkulaufzeit, Akku-Überwachung, Umbau, Plan Einstellungsseite
└── lichtwecker/
    ├── lichtwecker.ino     aktueller Sketch
    ├── secrets.example.h   Vorlage für Zugangsdaten
    └── secrets.h           lokal, nicht versioniert
```
