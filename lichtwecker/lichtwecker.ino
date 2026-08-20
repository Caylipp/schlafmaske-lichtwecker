#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include "time.h"
#include "esp_sleep.h"
#include "secrets.h"            // WIFI_SSID / WIFI_PASSWORD, siehe secrets.example.h

// ==========================================
// KONFIGURATION
// ==========================================
#define LED_PIN           2        // Wieder auf GPIO 2 gesetzt
#define BUTTON_PIN        9        // BOOT-Button (GPIO 9)
#define NUM_LEDS          32       

const uint8_t MAX_BRIGHTNESS = 76; 

const char* NTP_SERVER   = "de.pool.ntp.org";
const char* TIME_ZONE     = "CET-1CEST,M3.5.0,M10.5.0/3"; 

const int ALARM_HOUR      = 5;     // Weckstunde
const int ALARM_MINUTE    = 30;    // Weckminute

const bool TEST_MODE      = false;  // true = 10s Sleep + Zeitraffer-Wecker (1s = 1min)

// ==========================================
// RTC-SPEICHER (Überlebt den Deep Sleep)
// ==========================================
RTC_DATA_ATTR time_t rtcLastEpoch = 0;
RTC_DATA_ATTR bool rtcTimeValid = false;
RTC_DATA_ATTR uint64_t rtcLastSleepDurationSec = 0;

// ==========================================
// INITIALISIERUNG
// ==========================================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setStripColor(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  strip.setBrightness(brightness);
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void clearLEDs() {
  setStripColor(0, 0, 0, 0);
}

bool syncTimeWithWiFi(struct tm *timeinfo) {
  Serial.printf("[WIFI] Verbinde mit %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30) {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[NTP] Hole Uhrzeit...");
    configTzTime(TIME_ZONE, NTP_SERVER);

    int retry = 0;
    while (!getLocalTime(timeinfo) && retry < 20) {
      delay(500);
      retry++;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (retry < 20) {
      time_t now;
      time(&now);
      rtcLastEpoch = now;
      rtcTimeValid = true;
      Serial.printf("[NTP] Sync erfolgreich: %02d:%02d:%02d\n", 
                    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      return true;
    }
  }
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("\n[FEHLER] WLAN/NTP-Verbindung fehlgeschlagen.");
  return false;
}

bool getValidTime(struct tm *timeinfo) {
  if (syncTimeWithWiFi(timeinfo)) {
    return true;
  }

  if (rtcTimeValid) {
    rtcLastEpoch += rtcLastSleepDurationSec;
    localtime_r(&rtcLastEpoch, timeinfo);

    Serial.printf("[RTC-FALLBACK] Nutze interne Zeit: %02d:%02d:%02d\n", 
                  timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return true;
  }

  return false;
}

void goToDeepSleepUntilPreAlarm(struct tm *currentTime) {
  uint64_t sleepSeconds = 0;

  if (TEST_MODE) {
    sleepSeconds = 10;
  } else {
    time_t nowSec = mktime(currentTime);

    // Exakte Weckzeit für heute berechnen
    struct tm alarmTimeToday = *currentTime;
    alarmTimeToday.tm_hour = ALARM_HOUR;
    alarmTimeToday.tm_min = ALARM_MINUTE;
    alarmTimeToday.tm_sec = 0;
    time_t alarmSec = mktime(&alarmTimeToday);

    // Pre-Alarm (5 Minuten vor Weckzeit)
    time_t preAlarmSec = alarmSec - 300;

    if (nowSec < preAlarmSec) {
      // Vor dem heutigen Pre-Alarm -> Bis heute schlafen
      sleepSeconds = preAlarmSec - nowSec;
    } else {
      // Nach dem heutigen Pre-Alarm -> Bis morgen schlafen (+24h)
      sleepSeconds = (preAlarmSec + 86400) - nowSec;
    }
  }

  rtcLastSleepDurationSec = sleepSeconds;

  Serial.println("==========================================");
  Serial.printf("[SLEEP] Gehe fuer %llu Sekunden (%.1f Std) in Deep Sleep...\n", 
                sleepSeconds, sleepSeconds / 3600.0f);
  Serial.println("==========================================\n");
  Serial.flush();

  clearLEDs();
  
  // Aufwachen per Timer
  esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  
  // Aufwachen per Taste (GPIO 9 LOW) aus dem Deep Sleep
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_deep_sleep_start();
}

void runSunriseAlarm() {
  Serial.println("\n[ALARM] >>> SONNENAUFGANG GESTARTET <<<");
  unsigned long startMillis = millis();
  bool alarmRunning = true;
  int currentPhase = -1;

  while (alarmRunning) {
    // Button mit Tasten-Entprellung (Debouncing)
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50); // 50ms Warten gegen Fehlauslösung durch Signalrauschen
      if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("[BUTTON] Wecker manuell beendet!");
        break;
      }
    }

    unsigned long elapsedMillis = millis() - startMillis;
    float elapsedMinutes = TEST_MODE ? (elapsedMillis / 1000.0f) : (elapsedMillis / 60000.0f);

    uint8_t r = 0, g = 0, b = 0, bright = 0;
    int phase = 0;

    if (elapsedMinutes < 15.0f) {
      phase = 1;
      float progress = elapsedMinutes / 15.0f;
      r = 255; g = (uint8_t)(progress * 100); b = 0;
      bright = 3 + (uint8_t)(progress * (MAX_BRIGHTNESS * 0.4f - 3));
    }
    else if (elapsedMinutes < 25.0f) {
      phase = 2;
      float progress = (elapsedMinutes - 15.0f) / 10.0f;
      r = 255; g = 100 + (uint8_t)(progress * 80); b = (uint8_t)(progress * 100);
      bright = (uint8_t)(MAX_BRIGHTNESS * 0.4f + progress * (MAX_BRIGHTNESS * 0.4f));
    }
    else if (elapsedMinutes < 30.0f) {
      phase = 3;
      float progress = (elapsedMinutes - 25.0f) / 5.0f;
      r = 255 - (uint8_t)(progress * 30); g = 180 + (uint8_t)(progress * 75); b = 100 + (uint8_t)(progress * 155);
      bright = (uint8_t)(MAX_BRIGHTNESS * 0.8f + progress * (MAX_BRIGHTNESS * 0.2f));
    }
    else if (elapsedMinutes < 60.0f) {
      phase = 4;
      float pulse = (sin(millis() / 400.0f) + 1.0f) / 2.0f;
      r = 255; g = 255; b = 255;
      bright = 10 + (uint8_t)(pulse * (MAX_BRIGHTNESS - 10));
    }
    else {
      alarmRunning = false;
      Serial.println("[ALARM] Wecker automatisch beendet.");
      break;
    }

    if (phase != currentPhase) {
      currentPhase = phase;
      Serial.printf("[PHASE] Wechsel zu Phase %d\n", currentPhase);
    }

    setStripColor(r, g, b, bright);
    delay(30);
  }

  clearLEDs();
}

// ==========================================
// MAIN SETUP
// ==========================================

void setup() {
  // Zeitfenster für Uploads über USB CDC
  delay(3000); 

  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  strip.begin();
  clearLEDs();

  Serial.println("\n--- ESP32-C3 WAKEUPSERVICE ---");

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[WAKEUP] Durch Manuelle Taste aufgeweckt!");
  }

  struct tm timeinfo;
  if (!getValidTime(&timeinfo)) {
    Serial.println("[SLEEP] Erststart ohne WLAN: Versuche in 60s erneut...");
    rtcLastSleepDurationSec = 60;
    esp_sleep_enable_timer_wakeup(60 * 1000000ULL);
    esp_deep_sleep_start();
  }

  // Präzises Prüfen des Weckfensters
  time_t nowSec = mktime(&timeinfo);
  struct tm alarmTimeToday = timeinfo;
  alarmTimeToday.tm_hour = ALARM_HOUR;
  alarmTimeToday.tm_min = ALARM_MINUTE;
  alarmTimeToday.tm_sec = 0;
  time_t alarmSec = mktime(&alarmTimeToday);
  time_t preAlarmSec = alarmSec - 300; 

  bool inAlarmWindow = (nowSec >= preAlarmSec && nowSec <= (alarmSec + 300));

  if (!TEST_MODE && inAlarmWindow) {
    while (timeinfo.tm_hour != ALARM_HOUR || timeinfo.tm_min < ALARM_MINUTE) {
      delay(5000);
      getLocalTime(&timeinfo);
    }
    runSunriseAlarm();
  } else if (TEST_MODE) {
    runSunriseAlarm();
  }

  goToDeepSleepUntilPreAlarm(&timeinfo);
}

void loop() {
  // Wird wegen Deep Sleep nicht erreicht
}