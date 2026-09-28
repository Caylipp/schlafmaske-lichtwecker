# Konzept: Schlafmaske-Lichtwecker

## Idee
Schlafmaske mit eingebauten LEDs, die zur Weckzeit einen Sonnenaufgang simulieren und so sanft wecken.

## Ist-Stand (Code)
- ESP32 (GPIO 9 als BOOT-Button, deshalb vermutlich ESP32-C3) mit zwei NeoPixel-Ringen (je 8 LEDs, einer pro Auge; Helligkeit auf 76 von 255 begrenzt).
- Holt die Uhrzeit per WLAN über NTP, danach Deep Sleep bis kurz vor dem Alarm. Die Zeit wird im RTC-Speicher über den Schlaf gerettet.
- Die Sonnenaufgangs-Animation lässt sich per Taste beenden. Es gibt einen Testmodus mit Zeitraffer.
- Die Weckzeit ist fest im Code eingetragen. Die WLAN-Daten liegen inzwischen in `lichtwecker/secrets.h` (nicht versioniert).
- Der Code liegt in `lichtwecker/` (Tag `v1.0`). Details und bekannte Bugs stehen im [README](README.md).

## Bewertung
| Kriterium | Einschätzung |
|---|---|
| Stand | Funktionsfähiger Prototyp vorhanden. |
| Nutzen | Hoch für den Eigengebrauch, Lichtwecker sind wissenschaftlich gut belegt. |
| Konkurrenz | Lichtwecker gibt es viele (Philips), Masken mit Licht wenige (z. B. Manta, Lumos). |
| Risiken | Tragekomfort und Wärme, Akkusicherheit (LiPo nah am Kopf), zu helles bzw. blaues Licht direkt am Auge. |
| Produktpotenzial | Mittel. Hardware-Produkt mit hohem Aufwand (CE-Kennzeichnung, Gehäuse). Als Maker-Projekt oder Open-Source-Bausatz realistisch. |

**Fazit:** Das Projekt ist am weitesten fortgeschritten. Die nächsten Schritte sind Aufräumen, Konfigurierbarkeit und Hardware-Verfeinerung.

## Umsetzungsideen
- **Sofort**
  - ~~WLAN-Zugangsdaten in eine `secrets.h` auslagern und diese in `.gitignore` aufnehmen~~ (erledigt)
  - ~~Doppelte Version entfernen und stattdessen Git-Tags nutzen~~ (erledigt, Tag `v1.0`)
- **Konfiguration**
  - Weckzeit per BLE oder über ein kurzes WLAN-Captive-Portal einstellen (WiFiManager).
  - Später über eine kleine Web-App bzw. PWA per Web Bluetooth.
- **Licht**
  - Farbverlauf wie ein echter Sonnenaufgang: Dunkelrot, Orange, warmes Weiß.
  - Helligkeit gammakorrigiert und logarithmisch hochfahren, über 20–30 min.
- **Weitere Ideen**
  - Vibrationsmotor als Backup-Alarm, Snooze per Taste.
  - Wochentagsprofile.
  - Smart Wake per Beschleunigungssensor (im leichten Schlaf wecken).
- **Hardware**
  - Weniger und diffusere LEDs (Hitze, Blendung).
  - LiPo mit Schutzschaltung, USB-C-Laden, flexible Platine bzw. Stoff-Integration.
- **Tooling**: auf PlatformIO umsteigen, damit Abhängigkeiten reproduzierbar sind.
