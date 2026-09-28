# Bedienungsanleitung: Schlafmaske-Lichtwecker

Die Maske weckt dich mit einem simulierten Sonnenaufgang. Das Licht wird langsam heller, bis es zur Weckzeit weiß ist.

## Die zwei Knöpfe

Beide sitzen auf dem kleinen Board (XIAO) neben dem USB-C-Anschluss.

| Knopf | Beschriftung | Wofür |
|---|---|---|
| **RESET** | R | Maske „aufwecken“: Status anzeigen, Menü öffnen |
| **BOOT** | B | **Wecker ausschalten**, im Menü blättern und auswählen |

> **Reihenfolge:** Immer **erst RESET, dann BOOT.** Wer BOOT schon beim Drücken von RESET festhält, startet den Upload-Modus. Dann einfach nochmal nur RESET drücken.

## Täglicher Ablauf

### 1. Laden
- USB-C einstecken. Die **rote LED** auf dem Board leuchtet beim Laden und geht aus, wenn der Akku voll ist.
- **Jeden Tag laden, am besten abends ~1–1,5 Stunden vor dem Schlafengehen** (z. B. 20:30–22:00). Warum das so ist, steht unten unter [Akku](#akku).

### 2. Abend-Check (vor dem Schlafen)
USB abziehen, **RESET** drücken und nichts weiter anfassen:

| Was du siehst | Bedeutung |
|---|---|
| erste LED blau (3 s) | Menü-Fenster, einfach abwarten |
| Ringe füllen sich **grün** | Verbindung zum WLAN wird aufgebaut |
| Ringe **voll grün** | WLAN und Uhrzeit ok ✔ |
| Ringe **voll gelb** | kein WLAN, die interne Uhr läuft trotzdem, der Wecker funktioniert |
| **orange** LEDs | **Stunden** bis zum Wecken (1 LED = 1 Stunde, erst linker/erster Ring, dann der zweite) |
| **blaue** LEDs | dazu **Minuten** in 10er-Schritten (3 blaue = 30 min) |
| Ringe **rot leuchtend** (3 s) | **morgen kein Wecker**: Wecker ist aus oder der nächste ist ausgesetzt |
| 3× **rot blinkend** | ⚠ keine Uhrzeit, **der Wecker funktioniert nicht**. WLAN prüfen, RESET wiederholen |
| 3× **lila blinkend** | ⚠ Spannung ist eingebrochen, **Akku laden** |

**Beispiel:** 22:00, Wecker 08:00 → grün → **10 orange** (erster Ring voll + 2 im zweiten) → **0 blau** = 10 h. Gute Nacht!

Danach wird alles dunkel und die Maske schläft bis kurz vor dem Sonnenaufgang. Dann gleicht sie die Uhr übers WLAN ab. **Die LEDs bleiben dabei aus.**

> **Zurzeit läuft ein Test:** Der zusätzliche Uhr-Abgleich mitten in der Nacht ist abgeschaltet. Siehe [Uhr-Test](#uhr-test-drift-protokoll).

### 3. Aufwachen
- 30 Minuten vor der Weckzeit beginnt ein **Sonnenaufgang**: dunkelrot → orange → pfirsich → weiß.
- Zur Weckzeit ist das Licht **weiß** und **pulsiert** noch 30 Minuten.
- **BOOT drücken = Wecker aus.** Das geht jederzeit, auch mitten im Sonnenaufgang.
- In den 2 Minuten vor dem Sonnenaufgang (das Board ist dann schon wach) setzt BOOT den heutigen Wecker ganz aus.

## Menü

**RESET** drücken, dann **BOOT drücken, solange die erste LED blau ist**. Nach 2× hellblauem Blinken bist du im Menü.

- **Die Zahl der hellblauen LEDs** zeigt den Modus (1–6).
- **BOOT kurz** = nächster Modus.
- **BOOT lang** (1 s, bis alles weiß aufblitzt) = Modus starten.
- 15 s nichts drücken = Menü verlassen, danach kommt der normale Abend-Check.

| Modus | Name | Was passiert |
|---|---|---|
| 1 | **LED-Test** | Rot, Grün, Blau, Weiß → jede LED einzeln → 10 s volle Helligkeit. Zeigt kaputte LEDs und ob der Akku die Last schafft |
| 2 | **Vorschau** | Sonnenaufgang im Zeitraffer (1 Minute) |
| 3 | **Probelauf** | Maske schläft wirklich ein und weckt dich nach ~3 Minuten mit einem Zeitraffer-Sonnenaufgang. Testet die komplette Kette wie nachts |
| 4 | **Einstellungen (WLAN)** | Weckzeit usw. am Handy ändern, siehe unten |
| 5 | **Wecker an/aus** | schaltet um. Danach 2 s **grün = an**, **rot = aus** |
| 6 | **WLAN wechseln** | wechselt das bevorzugte WLAN. Danach 2 s **1 grüne LED = WLAN 1**, **2 grüne = WLAN 2** |

BOOT während eines Tests bricht ihn ab.

## Weckzeit ändern (am Handy)

1. **RESET**, dann **BOOT** (erste LED blau) → Menü.
2. BOOT kurz drücken, bis **4 LEDs** hellblau leuchten, dann **BOOT lang**.
3. Die Ringe füllen sich grün, danach **„atmen“ sie blau**. Die Maske ist jetzt online.
4. Am iPhone unter **Einstellungen → WLAN** das Netz **„Lichtwecker“** wählen. **Die Seite öffnet sich von selbst.**
   - Falls nicht: in Safari **http://192.168.4.1** öffnen.
   - Alternativ im Heim-WLAN: **http://lichtwecker.local**. Das klappt aber nicht mit jedem Repeater.
5. Einstellen und **Speichern** drücken. Die Maske blinkt zur Bestätigung einmal grün.
6. **Fertig** drücken oder BOOT an der Maske. Die Maske zeigt dann den Status mit der neuen Weckzeit und schläft. Das iPhone wechselt von selbst zurück ins Heim-WLAN.

Auf der Seite:

| Einstellung | Bedeutung |
|---|---|
| Weckzeit | Uhrzeit, zu der das Licht voll hell ist |
| Wecker an | Haken weg = Wecker komplett aus, bis du ihn wieder einschaltest |
| Nächsten Wecker aussetzen | nur der nächste Morgen fällt aus (z. B. Wochenende), danach geht es normal weiter |
| Sonnenaufgang (min) | wie lange es vorher hell wird (5–60, Standard 30) |
| Licht nach Weckzeit (min) | wie lange es danach weiß pulsiert (0–60, Standard 30) |
| Max. Helligkeit | 10–150, Standard 76. Heller verbraucht mehr Strom und wird wärmer |
| WLAN | welches der zwei hinterlegten WLANs zuerst benutzt wird (geht auch mit Menü-Modus 6) |

Außerdem gibt es Buttons für **Vorschau** und **LED-Test** sowie das **Drift-Protokoll** (siehe unten).

Die Einstellungen bleiben gespeichert, auch nach RESET, leerem Akku oder neuem Hochladen der Firmware.
Die Seite ist bis zu 5 Minuten nach dem letzten Aufruf erreichbar, danach schläft die Maske von selbst.

## WLAN

In der Maske sind zwei WLANs hinterlegt:
- **WLAN 1** ist das Standard-WLAN.
- **WLAN 2** ist das Repeater-WLAN im Schlafzimmer.

Die Maske versucht immer zuerst das gewählte WLAN. Klappt das nicht, probiert sie automatisch das andere. Umschalten geht mit Menü-Modus 6 oder auf der Einstellungsseite.

## Uhr-Test (Drift-Protokoll)

Die Maske hat keinen Uhrenquarz. Im Tiefschlaf kann ihre Uhr deshalb etwas vor- oder nachgehen. Um herauszufinden, wie viel, läuft gerade ein Test:
- Der zusätzliche nächtliche Uhr-Abgleich ist **abgeschaltet**.
- Morgens beim Aufwachen gleicht die Maske ihre Uhr per WLAN ab und **speichert, wie weit sie danebenlag**. Die Werte bleiben auch nach RESET erhalten, bis zu 30 Messungen.
- **Ansehen:** Menü-Modus 4, auf der Einstellungsseite unter **„Uhr-Abweichung (Test)“**. „Plus“ heißt: Die Uhr ging nach, der Wecker kam also so viele Sekunden zu spät.
- Nach ein paar Nächten werten wir das gemeinsam aus. Ist die Abweichung klein (unter ~1 Minute pro Nacht), bleibt es so. Sonst wird der Nacht-Abgleich wieder eingeschaltet.

Wichtig für eine saubere Messung: abends einmal **RESET** drücken (Abend-Check). Damit startet die Messung mit einer frisch gestellten Uhr.

## Akku

- **Laden:** mit USB-C. Er lädt mit ~350 mA, von leer auf voll dauert es ~1,5–2 Stunden.
- **Jeden Tag laden.** Die LED-Ringe ziehen auch im ausgeschalteten Zustand ständig ein wenig Strom (~10 mA), deshalb hält der Akku nur ~1–1,5 Tage.
- **Nur 30 Minuten vor dem Schlafen reicht nicht.** Das bringt ~150 mAh, verbraucht werden pro Tag ~300 mAh. Nach 2–3 Tagen wäre der Akku leer. Mit ~1–1,5 Stunden pro Tag bleibt er dauerhaft im grünen Bereich.
- **Bestes Timing: abends, direkt vor dem Schlafengehen.** Dann ist er in der Nacht voll, wenn er gebraucht wird. Morgens laden ist schlechter, weil er über den Tag wieder Strom verliert.
- Während die Maske am Kabel hängt, funktioniert der Wecker ganz normal.
- 3× **lila** beim Abend-Check heißt: Die Spannung ist eingebrochen, sofort laden.

Mit einem kleinen Umbau (ein Transistor aus dem Elegoo-Kit) würden 30 Minuten pro Tag reichen. Details: [docs/akku-und-ausbauplan.md](docs/akku-und-ausbauplan.md).

## Wenn etwas nicht klappt

| Problem | Lösung |
|---|---|
| Nach RESET 3× rot blinkend | Kein WLAN und keine gespeicherte Uhrzeit. In Reichweite des Schlafzimmer-WLANs sein und RESET wiederholen |
| Ringe gelb statt grün | WLAN nicht erreicht. Der Wecker läuft trotzdem, sollte aber mindestens alle paar Tage mal grün zeigen |
| Einstellungsseite lädt nicht | Am iPhone mit dem WLAN **„Lichtwecker“** verbinden und **http://192.168.4.1** öffnen. Atmen die Ringe noch blau? Sonst Menü → Modus 4 neu starten |
| Maske reagiert gar nicht | Akku leer, USB-C einstecken und RESET drücken |
| Sonnenaufgang zu hell oder zu dunkel | Max. Helligkeit auf der Einstellungsseite anpassen |
| Nach dem LED-Test blinkt es lila | Der Akku schafft volle Helligkeit nicht, zuerst laden |

## Firmware neu hochladen

Die Anleitung dazu steht im [README](README.md#kompilieren--flashen). Kurz: Arduino IDE, Board **XIAO_ESP32C3**, dann **RESET** drücken und direkt **Hochladen** klicken.
