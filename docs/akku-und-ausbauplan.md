# Akku, Stromsparen und Ausbauplan

Stand: 28.09.2026. Grundlage ist die aktuelle Hardware:
- Seeed XIAO ESP32C3
- 2× Joycabin WS2812-Ring (8 LEDs, 5050), **Plus am 3V3-Pin**
- LiPo **500 mAh an den BAT-Pads**

Alle Stromwerte sind Schätzungen aus Datenblättern und typischen Messwerten, nicht selbst gemessen.

## 1. Wie lange hält der Akku?

### Verbrauch

| Zustand | Strom | Dauer pro Tag | Verbrauch pro Tag |
|---|---|---|---|
| XIAO im Deep Sleep | ~0,05 mA (Seeed: 44 µA) | ~23 h | ~1 mAh |
| **16 LEDs im Ruhezustand** (bekommen am 3V3-Pin auch im Schlaf Strom, WS2812 ≈ 0,5–1 mA pro LED) | **~10 mA** (8–16 mA) | 24 h | **~240 mAh** |
| WLAN-Sync (3–4× pro Nacht, je ~5–10 s) | ~100 mA | ~30 s | ~1 mAh |
| Abend-Check (Reset + Statusanzeige) | ~80 mA | ~30 s | < 1 mAh |
| Sonnenaufgang (30 min, gammakorrigiert dunkel beginnend) + Chip wach | ~60–80 mA im Mittel | 30 min | ~35 mAh |
| Weißes Pulsieren nach der Weckzeit | ~190 mA + Chip | 5 min (per Taste aus) bis 30 min | ~20–100 mAh |

Nutzbar sind etwa **425 mAh**, also ~85 % der 500 mAh. Weil die LEDs am 3,3-V-Regler hängen, bricht die Spannung schon bei ~3,5 V Akkuspannung ein, der Rest ist praktisch nicht nutzbar.

### Ergebnis

| Aufbau | Verbrauch pro Tag | Laufzeit | Jeden 2. Tag laden? |
|---|---|---|---|
| **So wie jetzt** | ~300–380 mAh | **~1,1–1,4 Tage** | **Nein, reicht nicht.** Tagsüber laden, nachts tragen, **jeden Tag** |
| Mit LED-Abschaltung (Transistor, siehe 2b) | ~60–140 mAh | ~3–7 Tage | Ja, mit Reserve |
| Mit LED-Abschaltung, Wecker per Taste nach ~5 min beendet | ~60 mAh | ~7 Tage | Ja |

**Wichtigste Erkenntnis:** ~80 % des Akkus verbrauchen die LEDs, während sie *aus* sind. Die ruhenden WS2812-Chips ziehen dauerhaft Strom, der Wecker selbst kostet wenig.

Beispiel mit dem aktuellen Aufbau: Du lädst bis 18 Uhr und trägst die Maske nachts. Bis zum Wecken um 05:30 sind ~150–200 mAh verbraucht, das geht gut. In der zweiten Nacht wäre der Akku gegen Morgen leer.

## 2. Möglichkeiten zur Akku-Überwachung

| # | Möglichkeit | Was man erfährt | Aufwand | Empfehlung |
|---|---|---|---|---|
| a | **Spannungsteiler + ADC** | Akkuspannung → Ladestand in %, Warnung, Schutz vor Tiefentladung | 2 Widerstände + 1 Kondensator aus deinem Elegoo-Kit | **Ja, zuerst** |
| b | **LED-Abschaltung per Transistor** | keine Anzeige, aber ~4–6× längere Laufzeit | 1 Transistor + 1 Widerstand aus dem Kit | **Ja, zusammen mit a** |
| c | Brownout-Erkennung | Board meldet nach Neustart „Spannung eingebrochen“ (lila) | schon eingebaut | läuft schon |
| d | Lade-LED des XIAO | leuchtet rot beim Laden, geht aus, wenn voll | nichts | beim Laden draufschauen |
| e | Fuel-Gauge-Modul (z. B. MAX17048, I²C) | genauester Ladestand in % | ~5 € + 4 Kabel | nur, wenn a zu ungenau ist |
| f | Laufzeit schätzen per Software (Wachzeit mitzählen) | grobe Schätzung ohne Löten | nur Software | unzuverlässig, nicht empfohlen |

### 2a. Spannungsteiler (Akkustand messen)

Der XIAO kann seine Akkuspannung nicht selbst messen. Mit zwei gleichen Widerständen wird sie halbiert (4,2 V → 2,1 V) und an einem Analog-Pin gelesen.

```
BAT+ ──[100 kΩ]──┬──[100 kΩ]── GND
                 │
                 ├──[100 nF (104)]── GND     (beruhigt die Messung)
                 │
               D1 (GPIO 3)
```

- Verbrauch: dauerhaft ~20 µA, das sind ~0,5 mAh pro Tag, vernachlässigbar.
- Umrechnung LiPo: 4,2 V = 100 %, 3,9 V ≈ 75 %, 3,75 V ≈ 50 %, 3,6 V ≈ 20 %, 3,5 V ≈ 5 %.
- Software danach:
  - Ladestand in der Statusanzeige als Ringfüllung (8 LEDs = 100 %).
  - Unter 20 % Warnblinken beim Abend-Check.
  - Unter ~3,4 V geht der Wecker in einen Dauerschlaf ohne Timer. Das schützt den Akku, und rotes Blinken beim Reset zeigt es an.

### 2b. LED-Abschaltung mit dem S8050 aus dem Kit

Der Transistor unterbricht die **Masse** der LED-Ringe, solange der Chip schläft.

```
LED-Ringe GND ──── C (Collector)
                   S8050
XIAO GND ───────── E (Emitter)
D3 (GPIO 5) ─[330 Ω]─ B (Basis)
```

- Anschlüsse des S8050: flache Seite zu dir, Beine nach unten, von links **E – B – C**. Bitte mit dem Aufdruck bzw. Datenblatt deines Exemplars vergleichen.
- Im Code: `LED_POWER_PIN 5`, `LED_POWER_ON HIGH`. Die Logik dafür ist schon eingebaut.
- Einschränkung: Der Transistor kostet ~0,2 V. Die LEDs laufen dann mit ~3,1 V statt 3,3 V, Blau und Grün können etwas schwächer werden. Das zeigt der LED-Test sofort.
- Später besser: ein P-MOSFET (z. B. AO3401, ~0,30 €) zwischen **BAT+** und den LEDs statt am 3V3-Pin. Die LEDs bekommen dann 3,7–4,2 V, was für WS2812 besser ist, und der Regler des XIAO wird entlastet.

### Pinbelegung nach dem Umbau

| XIAO-Pin | GPIO | Funktion |
|---|---|---|
| D0 | 2 | LED-Daten (wie bisher) |
| D1 | 3 | Akkuspannung (Spannungsteiler) |
| D2 | 4 | optional: eigener Taster (kann aus dem Deep Sleep wecken, „Button (small)“ aus dem Kit) |
| D3 | 5 | LED-Abschaltung (S8050) |

### Sicherheit (LiPo am Kopf)

- Nur Akkus **mit Schutzschaltung** verwenden. Das ist die kleine Platine unter dem Klebeband am Kabelende.
- Nicht unter der Bettdecke laden. Bei Aufblähung oder Dellen sofort austauschen.
- Den Akku so befestigen, dass er nicht geknickt oder gequetscht wird, wenn du auf der Seite liegst.

## 3. Weckzeit ohne Neukompilieren einstellen (Plan)

### Empfehlung: Einstellungsseite im Heim-WLAN

```
RESET → Menü → Modus 4 „Einstellungen“ (lang drücken)
      → Ringe „atmen“ langsam blau = Board ist online (max. 5 min)
      → iPhone: http://lichtwecker.local öffnen
      → Weckzeit setzen, speichern
      → Board zeigt die neue Weckzeit (orange Stunden / blaue 10-Minuten) und schläft wieder
```

Die Seite zeigt:
- Weckzeit
- „Morgen kein Wecker“ (Wochenende)
- optional Wochentagsprofile
- Sonnenaufgangsdauer und Helligkeit
- Akkustand (nach 2a)
- Buttons „LED-Test“ und „Probelauf“

Technik:
- `WebServer` und `ESPmDNS` aus dem ESP32-Core, keine neuen Bibliotheken.
- Speicherung dauerhaft in `Preferences` (NVS). Die Einstellungen überstehen Reset und Neu-Flashen.
- Stromkosten: 5 min WLAN ≈ 8 mAh, nur wenn man den Modus nutzt.

Warum nicht Bluetooth? Safari auf dem iPhone kann kein Web Bluetooth. Man bräuchte eine eigene App, das WLAN reicht.

### Rückfallebene: nur Taste + LEDs

Menü-Modus „Weckzeit“:
- Anzeige wie beim Status: orange = Stunde der Weckzeit, blau = 10-Minuten-Schritte.
- Kurz drücken = +10 min, lang drücken = speichern.

Das funktioniert auch ohne WLAN, ist aber umständlicher. Sinnvoll als Notlösung.

### Umsetzungsschritte

1. Einstellungen aus den Konstanten in `Preferences` verschieben (Standardwerte bleiben im Code).
2. Menü-Modus 4 mit Webserver, mDNS und einer einfachen HTML-Seite.
3. „Morgen kein Wecker“ und Wochentagsprofile.
4. Rückfallebene per Taste.

## 4. Weitere Verbesserungen für deinen Ablauf

Dein Ablauf: Die Maske ist zu Hause, du gehst ~22 Uhr ins Bett, machst den Abend-Check und stellst den Wecker bei Bedarf um.

| Idee | Nutzen | Aufwand |
|---|---|---|
| **Zwischenstopp gegen Uhrdrift** | Der interne Takt kann im Schlaf einige Prozent abweichen. Ohne Korrektur wäre der Wecker bei +3 % jeden Morgen ~10 min zu spät, bei +8 % fiele er oft aus. | **erledigt** (Simulation: in allen Fällen pünktlich) |
| Snooze | Taste kurz = 9 min Schlummern, lang = aus | klein |
| Backup-Alarm mit dem aktiven Buzzer aus dem Kit | piept leise, falls man nach X min nicht per Taste bestätigt hat | klein (1 Pin) |
| Eigener Taster an D2 | Abend-Check ohne RESET, Weckung direkt aus dem Deep Sleep | klein (Löten) |
| Akkuwarnung im Abend-Check | „heute noch laden“ erkennbar | nach 2a |
| Wärmere Lichtfarbe, weniger Blau | angenehmer direkt vor den Augen | klein |
| Stromverbrauch einmal messen (USB-Messgerät, ~10 €) | ersetzt die Schätzungen oben durch echte Werte | klein |

### Empfohlene Reihenfolge

1. Aktuelle Firmware flashen und mit dem Testmenü prüfen. **Bis zum Umbau täglich laden.**
2. Spannungsteiler, S8050 und optional den Taster anlöten (alles aus dem Kit, ~30 min).
3. Firmware: Akkuanzeige, LED-Abschaltung aktivieren, Taster-Weckung.
4. Einstellungsseite im WLAN.
5. Snooze und Buzzer.
