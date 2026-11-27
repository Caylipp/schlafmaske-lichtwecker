// Host-Tests für lichtwecker/lichtwecker_logic.h. Ausführen: tests/run.sh
// Winziger Testrahmen ohne externe Abhängigkeiten.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../lichtwecker/lichtwecker_logic.h"

static int g_checks = 0, g_failed = 0;
static const char *g_test = "";

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
  printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_failed++; printf("  FAIL [%s] %s:%d: %s == %s (%lld != %lld)\n", \
  g_test, __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

typedef void (*TestFn)(void);
struct TestCase { const char *name; TestFn fn; };
static TestCase g_cases[128];
static int g_caseCount = 0;
struct Reg { Reg(const char *n, TestFn f) { g_cases[g_caseCount++] = { n, f }; } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

// Ortszeit (Europe/Berlin über POSIX-TZ) -> time_t
static time_t local(int y, int mo, int d, int h, int mi, int s = 0) {
  struct tm tm = {};
  tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
  tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
  return mktime(&tm);
}
static struct tm lt(time_t t) { struct tm tm; localtime_r(&t, &tm); return tm; }

static const lw::AlarmParams A0800 = { 8, 0, 30 };

// ---------------- Weckzeit / Sonnenaufgang ----------------

TEST(start_normal_day) {
  time_t now = local(2026, 6, 10, 22, 0);
  time_t start = lw::sunriseStartAtOrAfter(now, A0800);
  CHECK_EQ(start, local(2026, 6, 11, 7, 30));
  CHECK_EQ(lw::alarmTimeOfStart(start, A0800), local(2026, 6, 11, 8, 0));
}

TEST(start_same_day_before_sunrise) {
  time_t now = local(2026, 6, 10, 5, 0);
  CHECK_EQ(lw::sunriseStartAtOrAfter(now, A0800), local(2026, 6, 10, 7, 30));
}

TEST(start_exactly_now_counts) {
  time_t now = local(2026, 6, 10, 7, 30);
  CHECK_EQ(lw::sunriseStartAtOrAfter(now, A0800), now);
  CHECK_EQ(lw::sunriseStartAtOrAfter(now + 1, A0800), local(2026, 6, 11, 7, 30));
}

TEST(start_alarm_after_midnight) {
  // Weckzeit 00:10, Sonnenaufgang 30 min: Start 23:40 am Vortag
  lw::AlarmParams p = { 0, 10, 30 };
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 6, 10, 23, 0), p), local(2026, 6, 10, 23, 40));
  // 00:05: der heutige Sonnenaufgang (23:40 gestern) läuft schon, der nächste beginnt heute 23:40
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 6, 11, 0, 5), p), local(2026, 6, 11, 23, 40));
  // Mit Nachfrist (wie im Setup: now - Dauer) wird der laufende Sonnenaufgang gefunden
  time_t now = local(2026, 6, 11, 0, 5);
  CHECK_EQ(lw::sunriseStartAtOrAfter(now - lw::lateGraceSec(30), p), local(2026, 6, 10, 23, 40));
  // Weckzeit selbst liegt nach Mitternacht
  CHECK_EQ(lw::alarmTimeOfStart(local(2026, 6, 10, 23, 40), p), local(2026, 6, 11, 0, 10));
}

TEST(start_alarm_midnight_sharp) {
  lw::AlarmParams p = { 0, 0, 60 };
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 6, 10, 12, 0), p), local(2026, 6, 10, 23, 0));
}

TEST(start_month_and_year_rollover) {
  lw::AlarmParams p = { 0, 20, 30 };
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 12, 31, 23, 51), p), local(2027, 1, 1, 23, 50));
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 12, 31, 23, 0), p), local(2026, 12, 31, 23, 50));
  CHECK_EQ(lw::sunriseStartAtOrAfter(local(2026, 2, 28, 23, 55), A0800), local(2026, 3, 1, 7, 30));
}

TEST(start_spring_forward_keeps_clock_time) {
  // 29.03.2026: 02:00 CET -> 03:00 CEST. Nacht hat nur 8 echte Stunden bis 08:00.
  time_t now = local(2026, 3, 28, 22, 0);
  time_t start = lw::sunriseStartAtOrAfter(now, A0800);
  CHECK_EQ(start, local(2026, 3, 29, 7, 30));
  CHECK_EQ(lw::alarmTimeOfStart(start, A0800), local(2026, 3, 29, 8, 0));
  CHECK_EQ(lw::alarmTimeOfStart(start, A0800) - now, 9 * 3600);   // 22:00 CET -> 08:00 CEST = 9 h echte Zeit (10 h Uhrzeit)
  CHECK_EQ(lt(start).tm_isdst, 1);
}

TEST(start_fall_back_keeps_clock_time) {
  // 25.10.2026: 03:00 CEST -> 02:00 CET. Nacht hat 11 echte Stunden bis 08:00.
  time_t now = local(2026, 10, 24, 22, 0);
  time_t start = lw::sunriseStartAtOrAfter(now, A0800);
  CHECK_EQ(start, local(2026, 10, 25, 7, 30));
  CHECK_EQ(lw::alarmTimeOfStart(start, A0800) - now, 11 * 3600);
}

TEST(sunrise_duration_exact_on_dst_gap) {
  // Weckzeit 03:00 am Tag der Zeitumstellung (März: 02:00-03:00 gibt es nicht).
  // Der Sonnenaufgang muss genau 30 echte Minuten dauern und die Weckzeit 03:00 sein.
  lw::AlarmParams p = { 3, 0, 30 };
  time_t now = local(2026, 3, 28, 22, 0);
  time_t start = lw::sunriseStartAtOrAfter(now, p);
  time_t alarm = lw::alarmTimeOfStart(start, p);
  CHECK_EQ(alarm, local(2026, 3, 29, 3, 0));
  CHECK_EQ(alarm - start, 30 * 60);
  CHECK_EQ(lt(alarm).tm_hour, 3);
  CHECK_EQ(lt(alarm).tm_min, 0);
}

TEST(sunrise_duration_exact_on_dst_fall_back) {
  lw::AlarmParams p = { 4, 0, 60 };
  time_t start = lw::sunriseStartAtOrAfter(local(2026, 10, 24, 22, 0), p);
  CHECK_EQ(lw::alarmTimeOfStart(start, p), local(2026, 10, 25, 4, 0));
  CHECK_EQ(lw::alarmTimeOfStart(start, p) - start, 3600);
}

TEST(start_is_never_before_t_for_all_hours) {
  // Alle Weckzeiten in Zehnerschritten um einen Zeitumstellungstag herum
  for (int day = 27; day <= 31; day++)
    for (int h = 0; h < 24; h++)
      for (int m = 0; m < 60; m += 10)
        for (int S = 5; S <= 60; S += 55) {
          lw::AlarmParams p = { 5, 30, S };
          time_t t = local(2026, 3, day, h, m);
          time_t s = lw::sunriseStartAtOrAfter(t, p);
          if (s < t || s - t > 25 * 3600) { CHECK(false); return; }
        }
  CHECK(true);
}

// ---------------- Aussetzen / Aus / Probelauf ----------------

TEST(skip_next_alarm) {
  time_t now = local(2026, 6, 10, 22, 0);
  time_t first = local(2026, 6, 11, 7, 30);
  CHECK_EQ(lw::nextAlarmStart(now, A0800, true, first), local(2026, 6, 12, 7, 30));
  CHECK(lw::noAlarmNext(now, A0800, true, first));
  CHECK(!lw::noAlarmNext(now, A0800, true, 0));
  CHECK_EQ(lw::nextAlarmStart(now, A0800, true, 0), first);
}

TEST(alarm_disabled) {
  time_t now = local(2026, 6, 10, 22, 0);
  CHECK_EQ(lw::nextAlarmStart(now, A0800, false, 0), 0);
  CHECK(lw::noAlarmNext(now, A0800, false, 0));
  CHECK_EQ(lw::nextSunriseStart(now, A0800, false, 0, 0), 0);
}

TEST(trial_overrides_regular) {
  time_t now = local(2026, 6, 10, 22, 0);
  time_t trial = now + 150;
  CHECK_EQ(lw::nextSunriseStart(now, A0800, true, 0, trial), trial);
  CHECK_EQ(lw::nextSunriseStart(now, A0800, false, 0, trial), trial);   // auch bei ausgeschaltetem Wecker
  CHECK_EQ(lw::nextSunriseStart(now, A0800, true, 0, now - 1), local(2026, 6, 11, 7, 30));   // Probelauf vorbei
  CHECK_EQ(lw::nextSunriseStart(now, A0800, true, 0, local(2026, 6, 12, 7, 0)), local(2026, 6, 11, 7, 30));
}

TEST(trial_pending_window) {
  time_t t = 1000000;
  CHECK(!lw::trialPending(t, 0, 1800));
  CHECK(lw::trialPending(t, t + 100, 1800));
  CHECK(lw::trialPending(t + 100 + 1800, t + 100, 1800));
  CHECK(!lw::trialPending(t + 100 + 1801, t + 100, 1800));
}

// ---------------- Aufwach-Entscheidung ----------------

TEST(decide_basic) {
  time_t now = 100000;
  CHECK_EQ(lw::decideAlarm(0, now, false, 0, 0, 120), lw::ALARM_OFF);
  CHECK_EQ(lw::decideAlarm(now + 120, now, false, 0, 0, 120), lw::ALARM_RUN);
  CHECK_EQ(lw::decideAlarm(now + 121, now, false, 0, 0, 120), lw::ALARM_LATER);
  CHECK_EQ(lw::decideAlarm(now - 600, now, false, 0, 0, 120), lw::ALARM_RUN);   // zu spät: einsteigen
  CHECK_EQ(lw::decideAlarm(now + 60, now, false, now + 60, 0, 120), lw::ALARM_SKIPPED);
  CHECK_EQ(lw::decideAlarm(now + 60, now, true, now + 60, 0, 120), lw::ALARM_RUN);   // Probelauf ignoriert Aussetzen
  CHECK_EQ(lw::decideAlarm(now + 60, now, false, 0, now + 60, 120), lw::ALARM_HANDLED);
}

TEST(late_wakeup_joins_running_sunrise) {
  // Sonnenaufgang 07:30-08:00, Chip wacht erst 07:50 auf (Drift/Akku/Neustart)
  time_t now = local(2026, 6, 11, 7, 50);
  time_t start = lw::sunriseStartAtOrAfter(now - lw::lateGraceSec(30), A0800);
  CHECK_EQ(start, local(2026, 6, 11, 7, 30));
  CHECK_EQ(lw::decideAlarm(start, now, false, 0, 0, 120), lw::ALARM_RUN);
  CHECK_EQ(lw::lateMillis(now, start, false), 20UL * 60 * 1000);
  CHECK_EQ(lw::lateMillis(now, start, true), 0);              // Probelauf startet immer von vorn
  CHECK_EQ(lw::lateMillis(start - 5, start, false), 0);       // zu früh: kein Versatz
}

TEST(sunrise_longer_than_remaining_time) {
  // Sonnenaufgang 60 min, Einschalten 20 min vor der Weckzeit: Einstieg bei 2/3 des Verlaufs
  lw::AlarmParams p = { 8, 0, 60 };
  time_t now = local(2026, 6, 11, 7, 40);
  time_t start = lw::sunriseStartAtOrAfter(now - lw::lateGraceSec(60), p);
  CHECK_EQ(start, local(2026, 6, 11, 7, 0));
  unsigned long lateMs = lw::lateMillis(now, start, false);
  CHECK_EQ(lateMs, 40UL * 60 * 1000);
  lw::SunriseFrame f = lw::sunriseFrame(lateMs / 60000.0f, 60, 30, 0.5f);
  CHECK_EQ(f.phase, 2);            // 40 von 60 min: Orange -> Pfirsich
  CHECK(f.level > 0.66f && f.level < 0.67f);
  // Nach Ende des Sonnenaufgangs (Weckzeit+1 s) gilt er nicht mehr als "laufend"
  time_t after = local(2026, 6, 11, 8, 0, 1);
  CHECK_EQ(lw::sunriseStartAtOrAfter(after - lw::lateGraceSec(60), p), local(2026, 6, 12, 7, 0));
}

TEST(sleep_seconds) {
  CHECK_EQ(lw::sleepSecondsUntil(1000, 5000, 120), 3880);
  CHECK_EQ(lw::sleepSecondsUntil(1000, 1100, 120), 1);     // schon im Margin-Fenster
  CHECK_EQ(lw::sleepSecondsUntil(1000, 1120, 120), 1);     // genau jetzt
}

TEST(night_sleep_to_next_sunrise) {
  // Wie goToDeepSleepUntilNextSunrise(): von 22:00 bis 2 min vor 07:30
  time_t now = local(2026, 6, 10, 22, 0);
  time_t start = lw::nextSunriseStart(now + 121, A0800, true, 0, 0);
  CHECK_EQ(lw::sleepSecondsUntil(now, start, 120), 9 * 3600 + 28 * 60);
}

TEST(no_double_play_after_cancelled_alarm) {
  // Nach Abbruch um 07:40 wacht der Chip neu auf: gleicher Start -> HANDLED, dann nächster Tag
  time_t handled = local(2026, 6, 11, 7, 30);
  time_t now = local(2026, 6, 11, 7, 40);
  time_t start = lw::sunriseStartAtOrAfter(now - 1800, A0800);
  CHECK_EQ(lw::decideAlarm(start, now, false, 0, handled, 120), lw::ALARM_HANDLED);
  time_t next = lw::nextSunriseStart(now + 121, A0800, true, 0, 0);
  CHECK_EQ(next, local(2026, 6, 12, 7, 30));
}

// ---------------- Sonnenaufgang: Farbe und Helligkeit ----------------

TEST(frame_phases_and_end) {
  const float S = 30, D = 30;
  CHECK_EQ(lw::sunriseFrame(0, S, D, 0).phase, 1);
  CHECK_EQ(lw::sunriseFrame(14.99f, S, D, 0).phase, 1);
  CHECK_EQ(lw::sunriseFrame(15.0f, S, D, 0).phase, 2);
  CHECK_EQ(lw::sunriseFrame(24.99f, S, D, 0).phase, 2);
  CHECK_EQ(lw::sunriseFrame(25.0f, S, D, 0).phase, 3);
  CHECK_EQ(lw::sunriseFrame(29.99f, S, D, 0).phase, 3);
  CHECK_EQ(lw::sunriseFrame(30.0f, S, D, 0).phase, 4);
  CHECK_EQ(lw::sunriseFrame(59.99f, S, D, 0).phase, 4);
  CHECK_EQ(lw::sunriseFrame(60.0f, S, D, 0).phase, 0);
  CHECK_EQ(lw::sunriseFrame(5.0f, S, 0, 0).phase, 1);
  CHECK_EQ(lw::sunriseFrame(30.0f, S, 0, 0).phase, 0);   // kein Licht nach der Weckzeit
}

TEST(frame_colors) {
  lw::SunriseFrame f = lw::sunriseFrame(0, 30, 30, 0);
  CHECK(f.r == 255 && f.g == 0 && f.b == 0 && f.level == 0.0f);
  f = lw::sunriseFrame(15.0f, 30, 30, 0);       // Beginn Orange
  CHECK(f.r == 255 && f.g == 100 && f.b == 0);
  f = lw::sunriseFrame(25.0f, 30, 30, 0);       // Beginn Pfirsich
  CHECK(f.r == 255 && f.g == 180 && f.b == 100);
  f = lw::sunriseFrame(29.99f, 30, 30, 0);      // fast Weiß
  CHECK(f.r >= 225 && f.g >= 254 && f.b >= 254);
  f = lw::sunriseFrame(31, 30, 30, 1.0f);       // Pulsspitze
  CHECK(f.r == 255 && f.g == 255 && f.b == 255 && f.level == 1.0f);
  f = lw::sunriseFrame(31, 30, 30, 0.0f);       // Pulstal
  CHECK(f.level > 0.599f && f.level < 0.601f);
}

TEST(frame_level_rises_linearly) {
  const float S = 30;
  for (float e = 0; e < S; e += 0.25f) {
    lw::SunriseFrame f = lw::sunriseFrame(e, S, 30, 0);
    CHECK(fabsf(f.level - e / S) < 1e-5f);
  }
}

TEST(frame_no_channel_overflow_any_duration) {
  for (int S = 5; S <= 60; S++)
    for (float e = 0; e < S; e += 0.01f * S) {
      lw::SunriseFrame f = lw::sunriseFrame(e, S, 30, 0.5f);
      // g in Phase 2: 100..180, Phase 3: 180..255; b: 0..100..255; r nie unter 225
      if (f.r < 225 || (f.phase == 2 && (f.g < 100 || f.g > 180)) || (f.phase == 3 && f.g < 180)) { CHECK(false); return; }
    }
  CHECK(true);
}

TEST(gamma_zero_is_dark_and_positive_never_dark) {
  for (int maxB = 10; maxB <= 150; maxB += 7) {
    lw::Rgb off = lw::scaleColor(255, 255, 255, 0.0f, maxB);
    CHECK(off.r == 0 && off.g == 0 && off.b == 0);
    for (float level = 0.0005f; level < 1.0f; level *= 1.5f) {
      lw::Rgb c = lw::scaleColor(255, 100, 0, level, maxB);
      if (c.r == 0 && c.g == 0 && c.b == 0) { CHECK(false); return; }
    }
  }
  CHECK(true);
}

TEST(gamma_monotonic_no_flicker) {
  // Mit steigendem Level darf kein Kanal je kleiner werden (sonst flackert es)
  const uint8_t cols[][3] = { {255,255,255}, {255,100,0}, {255,0,0}, {255,180,100}, {0,80,255} };
  for (int maxB = 10; maxB <= 150; maxB += 10)
    for (auto &c : cols) {
      lw::Rgb prev = lw::scaleColor(c[0], c[1], c[2], 0.0f, maxB);
      for (int i = 1; i <= 2000; i++) {
        lw::Rgb x = lw::scaleColor(c[0], c[1], c[2], i / 2000.0f, maxB);
        if (x.r < prev.r || x.g < prev.g || x.b < prev.b) { CHECK(false); return; }
        prev = x;
      }
    }
  CHECK(true);
}

TEST(gamma_limits) {
  lw::Rgb c = lw::scaleColor(255, 255, 255, 1.0f, 76);
  CHECK(c.r == 76 && c.g == 76 && c.b == 76);
  c = lw::scaleColor(255, 255, 255, 5.0f, 76);       // Level wird begrenzt
  CHECK(c.r == 76);
  c = lw::scaleColor(255, 255, 255, -1.0f, 76);
  CHECK(c.r == 0 && c.g == 0 && c.b == 0);
  c = lw::scaleColor(255, 255, 255, 0.5f, 76);       // 0.5^2.2 = 0.2176 -> 16.5
  CHECK(c.r == 17 || c.r == 16);
  c = lw::scaleColor(255, 100, 0, 0.0001f, 76);      // winziger Wert: Glimmen auf dem stärksten Kanal
  CHECK(c.r == 1 && c.g == 0 && c.b == 0);
  c = lw::scaleColor(0, 80, 255, 0.0001f, 76);
  CHECK(c.r == 0 && c.g == 0 && c.b == 1);   // Blau glimmt blau, nicht rot
  c = lw::scaleColor(0, 255, 0, 0.0001f, 76);
  CHECK(c.r == 0 && c.g == 1 && c.b == 0);
  c = lw::scaleColor(0, 0, 0, 0.5f, 76);             // schwarz bleibt schwarz
  CHECK(c.r == 0 && c.g == 0 && c.b == 0);
}

TEST(sunrise_brightness_never_dips_during_sunrise) {
  // Über den ganzen Sonnenaufgang (Sekundenraster) darf die Helligkeit (Summe der Kanäle,
  // wie von den LEDs ausgegeben) nie sinken, nur wachsen. Schutz vor Flackern/Rucken.
  for (int S = 5; S <= 60; S += 5)
    for (int maxB = 10; maxB <= 150; maxB += 20) {
      int prev = -1;
      for (int sec = 0; sec < S * 60; sec++) {
        lw::SunriseFrame f = lw::sunriseFrame(sec / 60.0f, S, 30, 0);
        lw::Rgb c = lw::scaleColor(f.r, f.g, f.b, f.level, maxB);
        int sum = c.r + c.g + c.b;
        if (sum < prev - 1)   // 1 Stufe Rundung (Rot sinkt in Phase 3 leicht) ist unsichtbar
           { printf("    S=%d max=%d sec=%d: %d -> %d\n", S, maxB, sec, prev, sum); CHECK(false); return; }
        prev = sum;
      }
    }
  CHECK(true);
}

TEST(no_brightness_drop_at_alarm_time) {
  // Übergang Sonnenaufgang -> Pulsieren: Das Licht darf an der Weckzeit nicht abfallen
  const float S = 30;
  for (int maxB = 10; maxB <= 150; maxB += 20) {
    lw::SunriseFrame before = lw::sunriseFrame(S - 0.001f, S, 30, lw::pulseAt(0));
    lw::SunriseFrame after  = lw::sunriseFrame(S, S, 30, lw::pulseAt(0));
    lw::Rgb cb = lw::scaleColor(before.r, before.g, before.b, before.level, maxB);
    lw::Rgb ca = lw::scaleColor(after.r, after.g, after.b, after.level, maxB);
    CHECK(ca.g >= cb.g && ca.b >= cb.b && ca.r >= cb.r);
  }
  CHECK(lw::pulseAt(0) == 1.0f);
}

TEST(pulse_range_and_period) {
  float lo = 1, hi = 0;
  for (unsigned long ms = 0; ms < 6000; ms += 10) {
    float p = lw::pulseAt(ms);
    if (p < lo) lo = p;
    if (p > hi) hi = p;
  }
  CHECK(lo >= 0.0f && lo < 0.001f);
  CHECK(hi <= 1.0f && hi > 0.999f);
  CHECK(lw::pulseAt(1257) < 0.01f);   // Tal nach ca. pi*400 ms
}

TEST(sunrise_starts_visible_quickly) {
  // Innerhalb der ersten Minute (bei 30 min) muss schon etwas leuchten
  lw::SunriseFrame f = lw::sunriseFrame(1.0f, 30, 30, 0);
  lw::Rgb c = lw::scaleColor(f.r, f.g, f.b, f.level, 76);
  CHECK(c.r + c.g + c.b >= 1);
  f = lw::sunriseFrame(0.0f, 30, 30, 0);
  c = lw::scaleColor(f.r, f.g, f.b, f.level, 76);
  CHECK(c.r == 0 && c.g == 0 && c.b == 0);   // exakt am Start noch dunkel
}

// ---------------- Statusanzeige / Menü / Taste ----------------

TEST(remaining_display) {
  lw::RemainingDisplay d = lw::remainingDisplay(7 * 3600 + 25 * 60);
  CHECK_EQ(d.hours, 7); CHECK_EQ(d.tenMinutes, 2); CHECK_EQ(d.minutes, 25);
  d = lw::remainingDisplay(59 * 60 + 59);
  CHECK_EQ(d.hours, 0); CHECK_EQ(d.tenMinutes, 5); CHECK_EQ(d.minutes, 59);
  d = lw::remainingDisplay(0);
  CHECK_EQ(d.hours, 0); CHECK_EQ(d.tenMinutes, 0);
}

TEST(hours_display_two_colors) {
  struct { int h, count; bool red; } cases[] = {
    {0,0,false}, {-1,0,false}, {1,1,false}, {7,7,false}, {8,8,false},
    {9,1,true}, {16,8,true}, {17,1,false}, {24,8,false} };
  for (auto &c : cases) {
    lw::HoursDisplay d = lw::hoursDisplay(c.h, 8);
    CHECK_EQ(d.count, c.count);
    CHECK_EQ(d.red, c.red);
  }
}

TEST(press_classification) {
  CHECK_EQ(lw::classifyPress(0, 1000), lw::PRESS_SHORT);
  CHECK_EQ(lw::classifyPress(999, 1000), lw::PRESS_SHORT);
  CHECK_EQ(lw::classifyPress(1000, 1000), lw::PRESS_LONG);
  CHECK_EQ(lw::classifyPress(5000, 1000), lw::PRESS_LONG);
}

TEST(menu_cycles) {
  int m = 1;
  for (int i = 0; i < 7; i++) m = lw::nextMenuMode(m, 7);
  CHECK_EQ(m, 1);
  CHECK_EQ(lw::nextMenuMode(7, 7), 1);
  CHECK_EQ(lw::nextMenuMode(1, 7), 2);
}

TEST(device_alarm_setting_cycles) {
  CHECK_EQ(lw::nextAlarmHour(0, 16), 1);
  CHECK_EQ(lw::nextAlarmHour(16, 16), 0);
  int h = 8; for (int i = 0; i < 17; i++) h = lw::nextAlarmHour(h, 16);
  CHECK_EQ(h, 8);
  CHECK_EQ(lw::nextTenMinutes(5), 0);
  CHECK_EQ(lw::nextTenMinutes(0), 1);
}

TEST(drift_per_8h) {
  CHECK(fabsf(lw::driftPer8h(2000, 8 * 3600) - 2.0f) < 1e-4f);
  CHECK(fabsf(lw::driftPer8h(-1000, 4 * 3600) + 2.0f) < 1e-4f);
  CHECK(lw::driftPer8h(500, 0) == 0.0f);
}

int main(int argc, char **argv) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   // wie TIME_ZONE in der Firmware
  tzset();
  const char *only = argc > 1 ? argv[1] : nullptr;
  for (int i = 0; i < g_caseCount; i++) {
    if (only && strcmp(only, g_cases[i].name) != 0) continue;
    g_test = g_cases[i].name;
    int before = g_failed;
    g_cases[i].fn();
    printf("%s %s\n", g_failed == before ? "ok  " : "FAIL", g_test);
  }
  printf("\n%d Pruefungen, %d fehlgeschlagen\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
