#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "secrets.h"            // WIFI_SSID / WIFI_PASSWORD, siehe secrets.example.h

// ==========================================
// KONFIGURATION
// ==========================================
// Board: Seeed Studio XIAO ESP32C3
#define LED_PIN           2        // Datenleitung NeoPixel (XIAO D0)
#define BUTTON_PIN        9        // BOOT-Button. Achtung: weckt NICHT aus dem Deep Sleep,
                                   // dafür muss die Taste an GPIO 0-5 (ESP32-C3)
#define LED_POWER_PIN     -1       // Optional: MOSFET/Load-Switch für die LED-Versorgung, -1 = keiner
#define LED_POWER_ON      HIGH     // Pegel, bei dem die LEDs Strom bekommen
#define NUM_LEDS          16       // 2 Ringe à 8 LEDs (ein Ring pro Auge), hintereinander geschaltet
#define LEDS_PER_RING     8

const uint8_t MAX_BRIGHTNESS = 76;   // Obergrenze pro Farbkanal (0-255)

const char* NTP_SERVER    = "de.pool.ntp.org";
const char* TIME_ZONE     = "CET-1CEST,M3.5.0,M10.5.0/3";

const int ALARM_HOUR      = 5;     // Weckstunde   (Sonnenaufgang ist zu dieser Zeit fertig)
const int ALARM_MINUTE    = 30;    // Weckminute

const int SUNRISE_MINUTES = 30;    // Dauer des Sonnenaufgangs vor der Weckzeit
const int DAYLIGHT_MINUTES = 30;   // Dauer des pulsierenden Lichts nach der Weckzeit
const int WAKE_MARGIN_SEC = 120;   // So viel früher aufwachen (WLAN/NTP-Sync)
const int LATE_GRACE_SEC  = SUNRISE_MINUTES * 60;  // Zu spät aufgewacht: bis dahin wird noch passend eingestiegen
const int DRIFT_CHECK_MIN_SEC = 3600; // Längeren Schlaf teilen: nach 90 % aufwachen und Uhr nachstellen,
                                      // weil der interne Takt im Deep Sleep um einige Prozent driftet

// Testmenü: nach RESET innerhalb von MENU_WINDOW_MS die BOOT-Taste drücken
const unsigned long MENU_WINDOW_MS  = 3000;   // gleichzeitig Upload-Fenster für USB
const unsigned long MENU_TIMEOUT_MS = 15000;  // ohne Tastendruck zurück in den Normalbetrieb
const unsigned long LONG_PRESS_MS   = 1000;   // lang drücken = Modus starten
const int TRIAL_DELAY_SEC = WAKE_MARGIN_SEC + 30;  // Probelauf: Sonnenaufgang startet so viel später

// ==========================================
// RTC-SPEICHER (Überlebt den Deep Sleep)
// ==========================================
RTC_DATA_ATTR time_t rtcHandledSunrise = 0;   // Start des zuletzt gespielten/übersprungenen Sonnenaufgangs
RTC_DATA_ATTR time_t rtcTrialStart = 0;       // Probelauf aus dem Testmenü, 0 = keiner

// ==========================================
// INITIALISIERUNG
// ==========================================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

enum TimeSource { TIME_NONE, TIME_RTC, TIME_NTP };
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };

volatile bool ntpSynced = false;

void onNtpSync(struct timeval *tv) {
  ntpSynced = true;
}

// ==========================================
// LEDS
// ==========================================

// Farbe für eine Helligkeit level = 0..1 (wahrgenommene Helligkeit),
// gammakorrigiert und auf MAX_BRIGHTNESS begrenzt. Kein setBrightness(),
// da das bei kleinen Werten Farbinformation verliert.
uint32_t scaledColor(uint8_t r, uint8_t g, uint8_t b, float level) {
  level = constrain(level, 0.0f, 1.0f);
  float scale = powf(level, 2.2f) * MAX_BRIGHTNESS / 255.0f;
  uint8_t rr = (uint8_t)lroundf(r * scale);
  uint8_t gg = (uint8_t)lroundf(g * scale);
  uint8_t bb = (uint8_t)lroundf(b * scale);
  if (level > 0.0f && rr == 0 && gg == 0 && bb == 0) rr = 1;  // ganz am Anfang schon ein Glimmen
  return strip.Color(rr, gg, bb);
}

void setStripColor(uint8_t r, uint8_t g, uint8_t b, float level) {
  strip.fill(scaledColor(r, g, b, level));
  strip.show();
}

// Die ersten n LEDs leuchten, der Rest ist aus (für Anzeigen wie Modus oder Stunden)
void showCount(int n, uint8_t r, uint8_t g, uint8_t b, float level) {
  strip.clear();
  n = constrain(n, 0, NUM_LEDS);
  if (n > 0) strip.fill(scaledColor(r, g, b, level), 0, n);  // count 0 hieße bei fill(): bis zum Ende
  strip.show();
}

// Auf jedem Ring leuchten die ersten n LEDs, beide Augen zeigen also dasselbe
void showRings(int n, uint8_t r, uint8_t g, uint8_t b, float level) {
  strip.clear();
  n = constrain(n, 0, LEDS_PER_RING);
  for (int ring = 0; ring < NUM_LEDS / LEDS_PER_RING; ring++) {
    for (int i = 0; i < n; i++) {
      strip.setPixelColor(ring * LEDS_PER_RING + i, scaledColor(r, g, b, level));
    }
  }
  strip.show();
}

void clearLEDs() {
  strip.clear();
  strip.show();
}

void blink(uint8_t r, uint8_t g, uint8_t b, int times) {
  for (int i = 0; i < times; i++) {
    setStripColor(r, g, b, 0.6f);
    delay(250);
    clearLEDs();
    delay(250);
  }
}

void ledPower(bool on) {
  if (LED_POWER_PIN < 0) return;
  gpio_hold_dis((gpio_num_t)LED_POWER_PIN);
  pinMode(LED_POWER_PIN, OUTPUT);
  digitalWrite(LED_POWER_PIN, on ? LED_POWER_ON : !LED_POWER_ON);
  if (!on) {
    // Pegel im Deep Sleep halten. Auf dem C3 braucht es für digitale GPIOs beides.
    gpio_hold_en((gpio_num_t)LED_POWER_PIN);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
    gpio_deep_sleep_hold_en();
#endif
  }
}

// ==========================================
// TASTE
// ==========================================

bool buttonPressed() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // Entprellung
    return digitalRead(BUTTON_PIN) == LOW;
  }
  return false;
}

void waitForRelease() {
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(50);
}

// Wartet bis zu timeoutMs auf einen kurzen oder langen Tastendruck.
// Beim langen Druck leuchtet der Streifen kurz auf, damit man weiß, wann man loslassen kann.
ButtonEvent waitForButton(unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (buttonPressed()) {
      unsigned long pressStart = millis();
      while (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - pressStart >= LONG_PRESS_MS) {
          setStripColor(255, 255, 255, 0.4f);
          waitForRelease();
          clearLEDs();
          return BTN_LONG;
        }
        delay(10);
      }
      return BTN_SHORT;
    }
    delay(10);
  }
  return BTN_NONE;
}

// Wartet ms Millisekunden. true = per Taste abgebrochen.
bool waitOrAbort(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (buttonPressed()) {
      waitForRelease();
      return true;
    }
    delay(10);
  }
  return false;
}

// ==========================================
// ZEIT
// ==========================================

// Systemzeit gilt als gültig, wenn sie nach 2024 liegt (nach Stromausfall startet sie bei 1970)
bool systemTimeValid() {
  return time(nullptr) > 1704067200;
}

// showProgress: Die Ringe füllen sich grün, während verbunden wird
// (bis 6 LEDs beim WLAN-Verbinden, 7 = WLAN verbunden, 8 = Uhrzeit per NTP da).
bool syncTimeWithWiFi(bool showProgress) {
  Serial.printf("[WIFI] Verbinde mit %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    if (showProgress) showRings(min(6, timeout / 4 + 1), 0, 255, 0, 0.5f);  // +1 LED alle 2 s
    delay(500);
    Serial.print(".");
    timeout++;
  }

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[NTP] Hole Uhrzeit...");
    if (showProgress) showRings(7, 0, 255, 0, 0.5f);
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTzTime(TIME_ZONE, NTP_SERVER);

    // Auf die echte NTP-Antwort warten. getLocalTime() reicht nicht: Die Systemzeit
    // läuft im Deep Sleep weiter und ist deshalb schon vor dem Sync "gültig".
    int retry = 0;
    while (!ntpSynced && retry < 20) {
      delay(500);
      retry++;
    }
    ok = ntpSynced;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (showProgress && ok) {
    showRings(LEDS_PER_RING, 0, 255, 0, 0.5f);   // Ringe voll grün: alles verbunden
    delay(1000);
  }
  if (!ok) Serial.println("\n[FEHLER] WLAN/NTP-Verbindung fehlgeschlagen.");
  return ok;
}

TimeSource getValidTime(bool showProgress) {
  // Zeitzone immer setzen, auch wenn WLAN ausfällt, sonst rechnet localtime in UTC
  setenv("TZ", TIME_ZONE, 1);
  tzset();

  if (syncTimeWithWiFi(showProgress)) {
    return TIME_NTP;
  }

  // Die RTC des ESP32 läuft im Deep Sleep weiter, die Systemzeit bleibt also erhalten
  if (systemTimeValid()) {
    Serial.println("[RTC-FALLBACK] Nutze interne Zeit.");
    return TIME_RTC;
  }

  return TIME_NONE;
}

void printTime(const char *label, time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  Serial.printf("%s %02d.%02d. %02d:%02d:%02d\n", label,
                tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

// Erster Sonnenaufgangs-Start (Weckzeit minus SUNRISE_MINUTES) zum Zeitpunkt t oder danach.
// mktime() normalisiert Tages-/Monatsüberläufe und Sommerzeit (tm_isdst = -1).
time_t sunriseStartAtOrAfter(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  tm.tm_hour = ALARM_HOUR;
  tm.tm_min = ALARM_MINUTE - SUNRISE_MINUTES;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;
  time_t start = mktime(&tm);
  while (start < t) {
    tm.tm_mday += 1;
    tm.tm_isdst = -1;
    start = mktime(&tm);
  }
  return start;
}

// Wie sunriseStartAtOrAfter, berücksichtigt aber einen anstehenden Probelauf
time_t nextSunriseStart(time_t t) {
  time_t start = sunriseStartAtOrAfter(t);
  if (rtcTrialStart >= t && rtcTrialStart < start) {
    start = rtcTrialStart;
  }
  return start;
}

// ==========================================
// DEEP SLEEP
// ==========================================

void goToDeepSleep(uint64_t sleepSeconds) {
  Serial.println("==========================================");
  Serial.printf("[SLEEP] Gehe fuer %llu Sekunden (%.1f Std) in Deep Sleep...\n",
                sleepSeconds, sleepSeconds / 3600.0f);
  Serial.println("==========================================\n");
  Serial.flush();

  clearLEDs();
  ledPower(false);

  // Noch gedrückte Taste loslassen lassen, sonst weckt sie sofort wieder (max. 5 s)
  unsigned long waitStart = millis();
  while (digitalRead(BUTTON_PIN) == LOW && millis() - waitStart < 5000) {
    delay(10);
  }

  // Aufwachen per Timer
  esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);

  // Aufwachen per Taste (LOW). Klappt auf dem ESP32-C3 nur an GPIO 0-5, an GPIO 9
  // meldet die IDF sonst "gpio 9 is an invalid deep sleep wakeup IO".
  if ((1ULL << BUTTON_PIN) & SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK) {
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  esp_deep_sleep_start();
}

void goToDeepSleepUntilNextSunrise() {
  time_t now = time(nullptr);
  time_t wakeAt = nextSunriseStart(now + WAKE_MARGIN_SEC + 1) - WAKE_MARGIN_SEC;
  printTime("[SLEEP] Naechster Sonnenaufgang ab", wakeAt + WAKE_MARGIN_SEC);

  uint64_t sleepSeconds = wakeAt > now ? wakeAt - now : 1;
  if (sleepSeconds > DRIFT_CHECK_MIN_SEC) {
    // Zwischenstopp: Selbst bei 10 % Drift wacht der Chip so noch vor dem Sonnenaufgang auf,
    // holt die Uhrzeit neu und schläft den kurzen Rest dann genau.
    sleepSeconds = sleepSeconds * 9 / 10;
    Serial.println("[SLEEP] Zwischenstopp zum Nachstellen der Uhr eingeplant.");
  }
  goToDeepSleep(sleepSeconds);
}

// ==========================================
// SONNENAUFGANG
// ==========================================

// Spielt den Sonnenaufgang ab. startOffsetMs > 0, wenn das Gerät zu spät aufgewacht ist:
// Dann wird an der passenden Stelle eingestiegen. fast = Zeitraffer (1 s = 1 min).
void runSunriseAlarm(unsigned long startOffsetMs, bool fast) {
  Serial.printf("\n[ALARM] >>> SONNENAUFGANG GESTARTET%s <<<\n", fast ? " (Zeitraffer)" : "");
  unsigned long startMillis = millis() - startOffsetMs;
  int currentPhase = -1;
  const float S = SUNRISE_MINUTES;

  ledPower(true);

  while (true) {
    if (buttonPressed()) {
      Serial.println("[BUTTON] Wecker manuell beendet!");
      waitForRelease();
      break;
    }

    unsigned long elapsedMillis = millis() - startMillis;
    float elapsedMinutes = fast ? (elapsedMillis / 1000.0f) : (elapsedMillis / 60000.0f);

    uint8_t r = 0, g = 0, b = 0;
    float level = 0.0f;
    int phase = 0;

    // Farbverlauf in drei Abschnitten (50 % / 33 % / 17 % der Dauer),
    // Helligkeit steigt gleichmäßig über den ganzen Sonnenaufgang.
    if (elapsedMinutes < S * 0.5f) {
      phase = 1;  // Rot -> Orange
      float progress = elapsedMinutes / (S * 0.5f);
      r = 255; g = (uint8_t)(progress * 100); b = 0;
    }
    else if (elapsedMinutes < S * 5.0f / 6.0f) {
      phase = 2;  // Orange -> Pfirsich
      float progress = (elapsedMinutes - S * 0.5f) / (S / 3.0f);
      r = 255; g = 100 + (uint8_t)(progress * 80); b = (uint8_t)(progress * 100);
    }
    else if (elapsedMinutes < S) {
      phase = 3;  // Pfirsich -> Weiß
      float progress = (elapsedMinutes - S * 5.0f / 6.0f) / (S / 6.0f);
      r = 255 - (uint8_t)(progress * 30); g = 180 + (uint8_t)(progress * 75); b = 100 + (uint8_t)(progress * 155);
    }
    else if (elapsedMinutes < S + DAYLIGHT_MINUTES) {
      phase = 4;  // Weckzeit erreicht: Weiß pulsierend
      float pulse = (sinf(millis() / 400.0f) + 1.0f) / 2.0f;
      r = 255; g = 255; b = 255;
      level = 0.6f + pulse * 0.4f;
    }
    else {
      Serial.println("[ALARM] Wecker automatisch beendet.");
      break;
    }

    if (phase < 4) {
      level = elapsedMinutes / S;
    }

    if (phase != currentPhase) {
      currentPhase = phase;
      Serial.printf("[PHASE] Wechsel zu Phase %d\n", currentPhase);
    }

    setStripColor(r, g, b, level);
    delay(30);
  }

  clearLEDs();
}

// ==========================================
// STATUS & TESTMENÜ
// ==========================================

// Anzeige nach jedem Reset, direkt nach dem grünen Füllen beim WLAN-Verbinden:
//   gelb  = kein WLAN, interne Uhr läuft     rot = keine Uhrzeit, Wecker geht nicht
//   dann  orange = volle Stunden bis zur Weckzeit (1 LED = 1 h, erst Ring 1, dann Ring 2)
//   dann  blau   = restliche Minuten in 10-Minuten-Schritten (auf beiden Ringen gleich)
void showStatus(TimeSource source) {
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println("[STATUS] Letzter Neustart durch Spannungseinbruch (Brownout)!");
    blink(255, 0, 255, 3);   // lila: Akku leer oder LEDs ziehen zu viel Strom
  }

  if (source == TIME_NONE) {
    Serial.println("[STATUS] Keine gueltige Uhrzeit.");
    blink(255, 0, 0, 3);     // rot
    return;
  }

  if (source == TIME_RTC) {
    showRings(LEDS_PER_RING, 255, 160, 0, 0.5f);   // gelb
    delay(1500);
  }

  time_t now = time(nullptr);
  time_t alarm = sunriseStartAtOrAfter(now) + SUNRISE_MINUTES * 60;
  long remaining = (long)(alarm - now);
  int hours = remaining / 3600;
  int tenMinutes = (remaining % 3600) / 600;
  printTime("[STATUS] Naechste Weckzeit:", alarm);
  Serial.printf("[STATUS] Zeitquelle: %s, noch %d Std. %d min\n",
                source == TIME_NTP ? "NTP" : "RTC", hours, (int)((remaining % 3600) / 60));

  clearLEDs();
  delay(300);
  showCount(hours, 255, 100, 0, 0.6f);        // orange: Stunden
  delay(4000);
  clearLEDs();
  delay(300);
  showRings(tenMinutes, 0, 80, 255, 0.6f);    // blau: 10-Minuten-Schritte
  delay(3000);
  clearLEDs();
}

// Modus 1: alle Farben, jede LED einzeln (tote LEDs / Unterbrechungen), dann Volllast.
// Startet das Board bei Volllast neu, reicht die Stromversorgung nicht.
void runLedTest() {
  Serial.println("[TEST] LED-Test: Farben");
  const uint8_t colors[][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255} };
  for (const auto &c : colors) {
    setStripColor(c[0], c[1], c[2], 1.0f);
    if (waitOrAbort(1000)) {
      clearLEDs();
      return;
    }
  }

  Serial.println("[TEST] LED-Test: Lauflicht");
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, scaledColor(255, 255, 255, 1.0f));
    strip.show();
    if (waitOrAbort(100)) {
      clearLEDs();
      return;
    }
  }

  Serial.println("[TEST] LED-Test: Volllast 10 s");
  setStripColor(255, 255, 255, 1.0f);
  waitOrAbort(10000);
  clearLEDs();
  Serial.println("[TEST] LED-Test fertig.");
}

// Testmenü: Modus = Anzahl hellblauer LEDs. Kurz drücken = nächster Modus, lang = starten.
// Rückgabe true = Probelauf gewählt.
bool runTestMenu() {
  const int MODE_COUNT = 3;
  int mode = 1;
  Serial.println("[MENU] Testmenue: 1 = LED-Test, 2 = Zeitraffer, 3 = Probelauf");
  blink(0, 180, 255, 2);   // Menü erkannt

  while (true) {
    showCount(mode, 0, 180, 255, 0.5f);
    Serial.printf("[MENU] Modus %d\n", mode);

    ButtonEvent ev = waitForButton(MENU_TIMEOUT_MS);
    if (ev == BTN_NONE) {
      clearLEDs();
      Serial.println("[MENU] Timeout, zurueck in den Normalbetrieb.");
      return false;
    }
    if (ev == BTN_SHORT) {
      mode = mode % MODE_COUNT + 1;
      continue;
    }

    if (mode == 1) runLedTest();
    else if (mode == 2) runSunriseAlarm(0, true);
    else return true;
  }
}

// ==========================================
// MAIN SETUP
// ==========================================

void setup() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool afterReset = (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED);

  setCpuFrequencyMhz(80);
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ledPower(true);
  strip.begin();
  clearLEDs();

  // Nach Reset/Einschalten: 3 s Fenster für USB-Uploads und fürs Testmenü.
  // Die erste LED leuchtet blau, solange man BOOT drücken kann.
  // (BOOT nicht schon beim Reset halten, sonst startet der Chip im Download-Modus.)
  bool menuRequested = false;
  if (afterReset) {
    showCount(1, 0, 0, 255, 0.5f);
    unsigned long t0 = millis();
    while (millis() - t0 < MENU_WINDOW_MS) {
      if (buttonPressed()) {
        waitForRelease();
        menuRequested = true;
        break;
      }
      delay(10);
    }
    clearLEDs();
  }

  Serial.println("\n--- ESP32-C3 WAKEUPSERVICE ---");

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[WAKEUP] Durch Manuelle Taste aufgeweckt!");
  }

  bool trialRequested = menuRequested && runTestMenu();

  TimeSource source = getValidTime(afterReset);
  if (afterReset) {
    showStatus(source);
  }
  if (source == TIME_NONE) {
    Serial.println("[SLEEP] Keine gültige Uhrzeit: Versuche in 60s erneut...");
    goToDeepSleep(60);
  }

  time_t now = time(nullptr);
  printTime("[ZEIT] Jetzt:", now);

  if (trialRequested) {
    rtcTrialStart = now + TRIAL_DELAY_SEC;
    printTime("[TEST] Probelauf: Sonnenaufgang (Zeitraffer) ab", rtcTrialStart);
    blink(0, 180, 255, 2);
  }

  // Anstehender Sonnenaufgang: Probelauf oder regulär (auch wenn er vor höchstens
  // LATE_GRACE_SEC hätte beginnen sollen)
  bool trial = rtcTrialStart != 0 && now <= rtcTrialStart + LATE_GRACE_SEC;
  if (!trial) rtcTrialStart = 0;
  time_t start = trial ? rtcTrialStart : sunriseStartAtOrAfter(now - LATE_GRACE_SEC);

  if (start == rtcHandledSunrise) {
    Serial.println("[ALARM] Heutiger Wecker wurde schon gespielt oder beendet.");
  }
  else if (start - now <= WAKE_MARGIN_SEC) {
    printTime("[ALARM] Sonnenaufgang ab", start);
    rtcHandledSunrise = start;
    if (trial) rtcTrialStart = 0;

    // Bis zum Start warten (höchstens WAKE_MARGIN_SEC). Taste = heutigen Wecker überspringen.
    bool skipped = false;
    while (time(nullptr) < start) {
      if (buttonPressed()) {
        Serial.println("[BUTTON] Heutiger Wecker übersprungen.");
        skipped = true;
        break;
      }
      delay(200);
    }

    if (!skipped) {
      now = time(nullptr);
      unsigned long lateMs = (!trial && now > start) ? (unsigned long)(now - start) * 1000UL : 0;
      if (lateMs > 0) Serial.printf("[ALARM] %lu s verspätet, steige passend ein.\n", lateMs / 1000);
      runSunriseAlarm(lateMs, trial);
    }
  }

  goToDeepSleepUntilNextSunrise();
}

void loop() {
  // Wird wegen Deep Sleep nicht erreicht
}
