// Reine Logik des Lichtweckers, ohne Arduino-Abhängigkeit.
// Wird von lichtwecker.ino benutzt und auf dem Rechner getestet (tests/run.sh).
// Nur Standardbibliothek: keine millis(), kein Serial, kein Preferences, keine Hardware.
#pragma once

#include <math.h>
#include <stdint.h>
#include <time.h>

namespace lw {

// ------------------------------------------------------------
// Helligkeit und Farbe
// ------------------------------------------------------------

struct Rgb {
  uint8_t r, g, b;
};

// Farbe für eine wahrgenommene Helligkeit level = 0..1, gammakorrigiert (2,2) und auf
// maxBrightness (0-255, pro Farbkanal) begrenzt. Ist level > 0, wird die Farbe nie ganz
// schwarz: der stärkste Kanal der Farbe bekommt dann 1 (Glimmen statt Aus), damit die LEDs
// am unteren Ende nicht flackern oder erst spät angehen. Ein reines Blau glimmt also blau, nicht rot.
inline Rgb scaleColor(uint8_t r, uint8_t g, uint8_t b, float level, int maxBrightness) {
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  float scale = powf(level, 2.2f) * maxBrightness / 255.0f;
  Rgb c;
  c.r = (uint8_t)lroundf(r * scale);
  c.g = (uint8_t)lroundf(g * scale);
  c.b = (uint8_t)lroundf(b * scale);
  if (level > 0.0f && c.r == 0 && c.g == 0 && c.b == 0) {
    // Glimmen auf dem stärksten Kanal der gewünschten Farbe (beim Sonnenaufgang ist das Rot)
    if (r >= g && r >= b) { if (r > 0) c.r = 1; }
    else if (g >= b)      c.g = 1;
    else                  c.b = 1;
  }
  return c;
}

// Ein Bild des Sonnenaufgangs. phase: 1 = Rot -> Orange, 2 = Orange -> Pfirsich,
// 3 = Pfirsich -> Weiß, 4 = Weiß pulsierend (Weckzeit erreicht), 0 = Wecker zu Ende.
struct SunriseFrame {
  int phase;
  uint8_t r, g, b;
  float level;   // 0..1, vor Gamma-Korrektur
};

// Pulsieren des weißen Lichts nach der Weckzeit, 0..1, Periode ca. 2,5 s. Beginnt beim Höchstwert
// (msSincePhase4 = 0), damit die Helligkeit an der Weckzeit nicht abfällt, sondern nahtlos vom
// Sonnenaufgang (Level 1,0) ins Pulsieren übergeht.
inline float pulseAt(unsigned long msSincePhase4) {
  return (cosf(msSincePhase4 / 400.0f) + 1.0f) / 2.0f;
}

// elapsedMinutes: Zeit seit Beginn des Sonnenaufgangs. S: Dauer des Sonnenaufgangs in Minuten.
// daylightMinutes: Dauer des pulsierenden Lichts danach. pulse: 0..1 (Sinus des Aufrufers).
// Farbverlauf in drei Abschnitten (50 % / 33 % / 17 % der Dauer), die Helligkeit steigt
// gleichmäßig über den ganzen Sonnenaufgang.
inline SunriseFrame sunriseFrame(float elapsedMinutes, float S, float daylightMinutes, float pulse) {
  SunriseFrame f = { 0, 0, 0, 0, 0.0f };
  if (elapsedMinutes < S * 0.5f) {
    f.phase = 1;
    float progress = elapsedMinutes / (S * 0.5f);
    f.r = 255; f.g = (uint8_t)(progress * 100); f.b = 0;
  } else if (elapsedMinutes < S * 5.0f / 6.0f) {
    f.phase = 2;
    float progress = (elapsedMinutes - S * 0.5f) / (S / 3.0f);
    f.r = 255; f.g = 100 + (uint8_t)(progress * 80); f.b = (uint8_t)(progress * 100);
  } else if (elapsedMinutes < S) {
    f.phase = 3;
    float progress = (elapsedMinutes - S * 5.0f / 6.0f) / (S / 6.0f);
    f.r = 255 - (uint8_t)(progress * 30); f.g = 180 + (uint8_t)(progress * 75); f.b = 100 + (uint8_t)(progress * 155);
  } else if (elapsedMinutes < S + daylightMinutes) {
    f.phase = 4;
    f.r = 255; f.g = 255; f.b = 255;
    f.level = 0.6f + pulse * 0.4f;
    return f;
  } else {
    return f;   // phase 0: zu Ende
  }
  f.level = elapsedMinutes / S;
  return f;
}

// ------------------------------------------------------------
// Weckzeit und Sonnenaufgang (Zeitzone/Sommerzeit über TZ + mktime)
// ------------------------------------------------------------

struct AlarmParams {
  int hour;
  int minute;
  int sunriseMinutes;
};

// Erster Sonnenaufgangs-Start zum Zeitpunkt t oder danach. Der Sonnenaufgang beginnt genau
// sunriseMinutes (echte Minuten) vor der Weckzeit. Die Weckzeit selbst wird als Ortszeit
// berechnet (mktime, tm_isdst = -1), so bleibt sie auch an Tagen mit Sommerzeitwechsel
// bei der eingestellten Uhrzeit. Die Weckzeit darf nach Mitternacht liegen, der Start
// dann am Vortag.
inline time_t sunriseStartAtOrAfter(time_t t, const AlarmParams &p) {
  struct tm tm;
  localtime_r(&t, &tm);
  tm.tm_hour = p.hour;
  tm.tm_min = p.minute;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  time_t start = mktime(&tm) - (time_t)p.sunriseMinutes * 60;
  while (start < t) {
    tm.tm_mday += 1;
    tm.tm_isdst = -1;
    start = mktime(&tm) - (time_t)p.sunriseMinutes * 60;
  }
  return start;
}

inline time_t alarmTimeOfStart(time_t start, const AlarmParams &p) {
  return start + (time_t)p.sunriseMinutes * 60;
}

// Nächster Sonnenaufgang, der wirklich stattfindet: 0 = Wecker aus, ausgesetzter Tag wird übersprungen
inline time_t nextAlarmStart(time_t t, const AlarmParams &p, bool enabled, time_t skipStart) {
  if (!enabled) return 0;
  time_t start = sunriseStartAtOrAfter(t, p);
  if (start == skipStart) start = sunriseStartAtOrAfter(start + 1, p);
  return start;
}

// Wie nextAlarmStart, berücksichtigt aber einen anstehenden Probelauf (trialStart, 0 = keiner)
inline time_t nextSunriseStart(time_t t, const AlarmParams &p, bool enabled, time_t skipStart, time_t trialStart) {
  time_t start = nextAlarmStart(t, p, enabled, skipStart);
  if (trialStart >= t && (start == 0 || trialStart < start)) start = trialStart;
  return start;
}

// true, wenn der nächste Wecker nicht klingelt (Wecker aus oder Termin ausgesetzt)
inline bool noAlarmNext(time_t now, const AlarmParams &p, bool enabled, time_t skipStart) {
  return !enabled || sunriseStartAtOrAfter(now, p) == skipStart;
}

// Zu spät aufgewacht: bis zum Ende des Sonnenaufgangs wird noch passend eingestiegen
inline int lateGraceSec(int sunriseMinutes) {
  return sunriseMinutes * 60;
}

// Probelauf aus dem Testmenü gilt noch, solange er höchstens graceSec her ist
inline bool trialPending(time_t now, time_t trialStart, int graceSec) {
  return trialStart != 0 && now <= trialStart + graceSec;
}

// Was beim Aufwachen mit dem anstehenden Sonnenaufgang (start, 0 = keiner) geschieht
enum AlarmDecision {
  ALARM_OFF,        // Wecker ist aus
  ALARM_SKIPPED,    // dieser Termin ist ausgesetzt
  ALARM_HANDLED,    // wurde heute schon gespielt oder beendet
  ALARM_RUN,        // Sonnenaufgang beginnt in höchstens wakeMarginSec oder läuft schon
  ALARM_LATER       // noch nicht dran: wieder schlafen
};

inline AlarmDecision decideAlarm(time_t start, time_t now, bool trial, time_t skipStart,
                                 time_t handledStart, int wakeMarginSec) {
  if (start == 0) return ALARM_OFF;
  if (!trial && start == skipStart) return ALARM_SKIPPED;
  if (start == handledStart) return ALARM_HANDLED;
  if (start - now <= wakeMarginSec) return ALARM_RUN;
  return ALARM_LATER;
}

// Millisekunden, die der Sonnenaufgang schon läuft, wenn das Gerät zu spät aufgewacht ist.
// Probelauf steigt immer von vorn ein.
inline unsigned long lateMillis(time_t now, time_t start, bool trial) {
  return (!trial && now > start) ? (unsigned long)(now - start) * 1000UL : 0;
}

// Wie lange der Chip schläft, um 'wakeMarginSec' vor dem Start aufzuwachen (mindestens 1 s)
inline uint64_t sleepSecondsUntil(time_t now, time_t start, int wakeMarginSec) {
  time_t wakeAt = start - wakeMarginSec;
  return wakeAt > now ? (uint64_t)(wakeAt - now) : 1;
}

// ------------------------------------------------------------
// Statusanzeige: Stunden und 10-Minuten-Schritte bis zur Weckzeit
// ------------------------------------------------------------

struct RemainingDisplay {
  int hours;
  int tenMinutes;
  int minutes;     // restliche Minuten (nur fürs Protokoll)
};

inline RemainingDisplay remainingDisplay(long remainingSec) {
  RemainingDisplay d;
  d.hours = (int)(remainingSec / 3600);
  d.tenMinutes = (int)((remainingSec % 3600) / 600);
  d.minutes = (int)((remainingSec % 3600) / 60);
  return d;
}

// Stunden auf einem Ring (8 LEDs): ab der 9. Stunde wechselt die Farbe auf rot und zählt von vorn
// (7 h = 7 orange, 9 h = 1 rot, 16 h = 8 rot, 17 h = 1 orange). count = 0: dunkel.
struct HoursDisplay {
  int count;
  bool red;
};

inline HoursDisplay hoursDisplay(int hours, int ledsPerRing) {
  HoursDisplay d = { 0, false };
  if (hours <= 0) return d;
  d.count = (hours - 1) % ledsPerRing + 1;
  d.red = ((hours - 1) / ledsPerRing) % 2 == 1;
  return d;
}

// ------------------------------------------------------------
// Bedienung: Taste und Menü
// ------------------------------------------------------------

enum PressKind { PRESS_SHORT, PRESS_LONG };

// Ein Tastendruck gilt ab longPressMs als lang
inline PressKind classifyPress(unsigned long heldMs, unsigned long longPressMs) {
  return heldMs >= longPressMs ? PRESS_LONG : PRESS_SHORT;
}

// Testmenü: Modus 1..modeCount, nach dem letzten wieder 1
inline int nextMenuMode(int mode, int modeCount) {
  return mode % modeCount + 1;
}

// Weckzeit am Gerät einstellen: Stunden 0..maxHour, 10-Minuten-Schritte 0..5, jeweils im Kreis
inline int nextAlarmHour(int hour, int maxHour) {
  return (hour + 1) % (maxHour + 1);
}

inline int nextTenMinutes(int tens) {
  return (tens + 1) % 6;
}

// ------------------------------------------------------------
// Uhr-Drift
// ------------------------------------------------------------

// Abweichung (NTP minus interne Uhr, in ms) hochgerechnet auf 8 Stunden Schlaf, in Sekunden
inline float driftPer8h(int32_t offsetMs, int32_t elapsedSec) {
  return elapsedSec > 0 ? (offsetMs / 1000.0f) * (8 * 3600.0f / elapsedSec) : 0;
}

}  // namespace lw
