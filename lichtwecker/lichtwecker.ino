#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "secrets.h"            // WIFI_SSID / WIFI_PASSWORD, siehe secrets.example.h

// ==========================================
// KONFIGURATION
// ==========================================
#define LED_PIN           2        // Datenleitung NeoPixel
#define BUTTON_PIN        9        // BOOT-Button. Achtung: weckt NICHT aus dem Deep Sleep,
                                   // dafür muss die Taste an GPIO 0-5 (ESP32-C3)
#define LED_POWER_PIN     -1       // Optional: MOSFET/Load-Switch für die LED-Versorgung, -1 = keiner
#define LED_POWER_ON      HIGH     // Pegel, bei dem die LEDs Strom bekommen
#define NUM_LEDS          32

const uint8_t MAX_BRIGHTNESS = 76;   // Obergrenze pro Farbkanal (0-255)

const char* NTP_SERVER    = "de.pool.ntp.org";
const char* TIME_ZONE     = "CET-1CEST,M3.5.0,M10.5.0/3";

const int ALARM_HOUR      = 5;     // Weckstunde   (Sonnenaufgang ist zu dieser Zeit fertig)
const int ALARM_MINUTE    = 30;    // Weckminute

const int SUNRISE_MINUTES = 30;    // Dauer des Sonnenaufgangs vor der Weckzeit
const int DAYLIGHT_MINUTES = 30;   // Dauer des pulsierenden Lichts nach der Weckzeit
const int WAKE_MARGIN_SEC = 120;   // So viel früher aufwachen (WLAN/NTP-Sync)
const int LATE_GRACE_SEC  = 600;   // Wecker startet noch, wenn bis zu 10 min zu spät aufgewacht

const bool TEST_MODE      = false;  // true = 10s Sleep + Zeitraffer-Wecker (1s = 1min)

// ==========================================
// RTC-SPEICHER (Überlebt den Deep Sleep)
// ==========================================
RTC_DATA_ATTR time_t rtcHandledSunrise = 0;   // Start des zuletzt gespielten/übersprungenen Sonnenaufgangs

// ==========================================
// INITIALISIERUNG
// ==========================================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

volatile bool ntpSynced = false;

void onNtpSync(struct timeval *tv) {
  ntpSynced = true;
}

// Setzt alle LEDs auf eine Farbe. level = 0..1 (wahrgenommene Helligkeit),
// wird gammakorrigiert und auf MAX_BRIGHTNESS begrenzt. Kein setBrightness(),
// da das bei kleinen Werten Farbinformation verliert.
void setStripColor(uint8_t r, uint8_t g, uint8_t b, float level) {
  level = constrain(level, 0.0f, 1.0f);
  float scale = powf(level, 2.2f) * MAX_BRIGHTNESS / 255.0f;
  uint8_t rr = (uint8_t)lroundf(r * scale);
  uint8_t gg = (uint8_t)lroundf(g * scale);
  uint8_t bb = (uint8_t)lroundf(b * scale);
  if (level > 0.0f && rr == 0 && gg == 0 && bb == 0) rr = 1;  // ganz am Anfang schon ein Glimmen
  strip.fill(strip.Color(rr, gg, bb));
  strip.show();
}

void clearLEDs() {
  strip.clear();
  strip.show();
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

bool buttonPressed() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // Entprellung
    return digitalRead(BUTTON_PIN) == LOW;
  }
  return false;
}

// Systemzeit gilt als gültig, wenn sie nach 2024 liegt (nach Stromausfall startet sie bei 1970)
bool systemTimeValid() {
  return time(nullptr) > 1704067200;
}

bool syncTimeWithWiFi() {
  Serial.printf("[WIFI] Verbinde mit %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[NTP] Hole Uhrzeit...");
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

  if (!ok) Serial.println("\n[FEHLER] WLAN/NTP-Verbindung fehlgeschlagen.");
  return ok;
}

// Liefert true, wenn eine brauchbare Uhrzeit vorliegt (per NTP oder RTC-Fallback)
bool getValidTime() {
  // Zeitzone immer setzen, auch wenn WLAN ausfällt, sonst rechnet localtime in UTC
  setenv("TZ", TIME_ZONE, 1);
  tzset();

  if (syncTimeWithWiFi()) {
    return true;
  }

  // Die RTC des ESP32 läuft im Deep Sleep weiter, die Systemzeit bleibt also erhalten
  if (systemTimeValid()) {
    Serial.println("[RTC-FALLBACK] Nutze interne Zeit.");
    return true;
  }

  return false;
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

  // Aufwachen per Taste (LOW). Klappt auf dem ESP32-C3 nur an GPIO 0-5.
  esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  if (err != ESP_OK) {
    Serial.printf("[WARNUNG] GPIO %d kann nicht aus dem Deep Sleep wecken (%s)\n",
                  BUTTON_PIN, esp_err_to_name(err));
    Serial.flush();
  }

  esp_deep_sleep_start();
}

void goToDeepSleepUntilNextSunrise() {
  if (TEST_MODE) {
    goToDeepSleep(10);
  }
  time_t now = time(nullptr);
  time_t wakeAt = sunriseStartAtOrAfter(now + WAKE_MARGIN_SEC + 1) - WAKE_MARGIN_SEC;
  printTime("[SLEEP] Naechster Sonnenaufgang ab", wakeAt + WAKE_MARGIN_SEC);
  goToDeepSleep(wakeAt > now ? wakeAt - now : 1);
}

// Spielt den Sonnenaufgang ab. startOffsetMs > 0, wenn das Gerät zu spät aufgewacht ist:
// Dann wird an der passenden Stelle eingestiegen.
void runSunriseAlarm(unsigned long startOffsetMs) {
  Serial.println("\n[ALARM] >>> SONNENAUFGANG GESTARTET <<<");
  unsigned long startMillis = millis() - startOffsetMs;
  int currentPhase = -1;
  const float S = SUNRISE_MINUTES;

  ledPower(true);

  while (true) {
    if (buttonPressed()) {
      Serial.println("[BUTTON] Wecker manuell beendet!");
      break;
    }

    unsigned long elapsedMillis = millis() - startMillis;
    float elapsedMinutes = TEST_MODE ? (elapsedMillis / 1000.0f) : (elapsedMillis / 60000.0f);

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
// MAIN SETUP
// ==========================================

void setup() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Zeitfenster für Uploads über USB CDC, nur nach Reset/Einschalten, nicht bei jedem Aufwachen
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    delay(3000);
  }

  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ledPower(true);
  strip.begin();
  clearLEDs();

  Serial.println("\n--- ESP32-C3 WAKEUPSERVICE ---");

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[WAKEUP] Durch Manuelle Taste aufgeweckt!");
  }

  if (!getValidTime()) {
    Serial.println("[SLEEP] Keine gültige Uhrzeit: Versuche in 60s erneut...");
    goToDeepSleep(60);
  }

  if (TEST_MODE) {
    runSunriseAlarm(0);
    goToDeepSleepUntilNextSunrise();
  }

  time_t now = time(nullptr);
  printTime("[ZEIT] Jetzt:", now);

  // Sonnenaufgang, der gerade ansteht oder vor höchstens LATE_GRACE_SEC hätte beginnen sollen
  time_t start = sunriseStartAtOrAfter(now - LATE_GRACE_SEC);

  if (start == rtcHandledSunrise) {
    Serial.println("[ALARM] Heutiger Wecker wurde schon gespielt oder beendet.");
  }
  else if (start - now <= WAKE_MARGIN_SEC) {
    printTime("[ALARM] Sonnenaufgang ab", start);
    rtcHandledSunrise = start;

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
      unsigned long lateMs = now > start ? (unsigned long)(now - start) * 1000UL : 0;
      if (lateMs > 0) Serial.printf("[ALARM] %lu s verspätet, steige passend ein.\n", lateMs / 1000);
      runSunriseAlarm(lateMs);
    }
  }

  goToDeepSleepUntilNextSunrise();
}

void loop() {
  // Wird wegen Deep Sleep nicht erreicht
}
