#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "time.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "secrets.h"            // WIFI_NETWORKS (Name + Passwort, bis zu mehreren), siehe secrets.example.h

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

const char* NTP_SERVER    = "de.pool.ntp.org";
const char* TIME_ZONE     = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* HOSTNAME      = "lichtwecker";   // Einstellungsseite im Heim-WLAN: http://lichtwecker.local
const char* AP_SSID       = "Lichtwecker";   // eigenes WLAN der Maske im Einstellungsmodus (offen, max. 5 min)
const int WIFI_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

// Standardwerte. Geändert werden sie über die Einstellungsseite, dort gespeicherte Werte haben Vorrang.
const int DEFAULT_ALARM_HOUR       = 8;    // Weckzeit (Sonnenaufgang ist zu dieser Zeit fertig)
const int DEFAULT_ALARM_MINUTE     = 0;
const int DEFAULT_SUNRISE_MINUTES  = 30;   // Dauer des Sonnenaufgangs vor der Weckzeit
const int DEFAULT_DAYLIGHT_MINUTES = 30;   // Dauer des pulsierenden Lichts nach der Weckzeit
const int DEFAULT_MAX_BRIGHTNESS   = 76;   // Obergrenze pro Farbkanal (0-255)

const int WAKE_MARGIN_SEC = 120;   // So viel früher aufwachen (WLAN/NTP-Sync)
// Nacht-Abgleich: Längeren Schlaf teilen, nach 90 % aufwachen und die Uhr per WLAN nachstellen,
// weil der interne Takt im Deep Sleep driften kann.
// TEST (seit 29.09.2026): abgeschaltet, um die echte Abweichung zu messen. Die Messwerte stehen im
// Drift-Protokoll (Einstellungsseite / serielle Ausgabe). Ist die Abweichung zu groß, wieder auf true setzen.
const bool DRIFT_CHECK_ENABLED = false;
const int DRIFT_CHECK_MIN_SEC = 3600; // Nacht-Abgleich nur bei Schlaf über 1 h
const int DRIFT_LOG_MIN_SEC   = 1800; // Drift-Protokoll: nur Abgleiche nach mindestens 30 min ohne Sync
const int DRIFT_LOG_SIZE      = 30;   // so viele Messungen werden aufgehoben (Ringpuffer)

// Testmenü: nach RESET innerhalb von MENU_WINDOW_MS die BOOT-Taste drücken
const unsigned long MENU_WINDOW_MS  = 3000;   // gleichzeitig Upload-Fenster für USB
const unsigned long MENU_TIMEOUT_MS = 15000;  // ohne Tastendruck zurück in den Normalbetrieb
const unsigned long LONG_PRESS_MS   = 1000;   // lang drücken = Modus starten
const unsigned long WEB_TIMEOUT_MS  = 5UL * 60 * 1000;  // Einstellungsseite: so lange ohne Aufruf online
const int TRIAL_DELAY_SEC = WAKE_MARGIN_SEC + 30;  // Probelauf: Sonnenaufgang startet so viel später

// ==========================================
// RTC-SPEICHER (Überlebt den Deep Sleep)
// ==========================================
RTC_DATA_ATTR time_t rtcHandledSunrise = 0;   // Start des zuletzt gespielten/übersprungenen Sonnenaufgangs
RTC_DATA_ATTR time_t rtcTrialStart = 0;       // Probelauf aus dem Testmenü, 0 = keiner
RTC_DATA_ATTR time_t rtcLastSyncEpoch = 0;    // Zeitpunkt des letzten erfolgreichen NTP-Abgleichs

// Vor der ersten Funktion, weil die Arduino-IDE dort die Funktionsprototypen einfügt
enum TimeSource { TIME_NONE, TIME_RTC, TIME_NTP };
enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG };
enum MenuResult { MENU_NORMAL, MENU_TRIAL };
enum WebAction { WEB_NONE, WEB_LED_TEST, WEB_PREVIEW, WEB_DONE };

// Eine Messung im Drift-Protokoll: Wie weit lag die interne Uhr beim NTP-Abgleich daneben?
struct DriftEntry {
  int64_t syncTime;    // Zeitpunkt des Abgleichs (Unixzeit)
  int32_t elapsedSec;  // Zeit seit dem vorherigen Abgleich
  int32_t offsetMs;    // NTP minus interne Uhr: > 0 = interne Uhr ging nach (Wecker käme zu spät)
};

// ==========================================
// EINSTELLUNGEN (dauerhaft im Flash, übersteht Reset und Neu-Flashen)
// ==========================================
struct Settings {
  int alarmHour;
  int alarmMinute;
  bool enabled;          // Wecker an/aus
  int sunriseMinutes;
  int daylightMinutes;
  int maxBrightness;
  time_t skipStart;      // dieser eine Sonnenaufgang fällt aus ("nächsten Wecker aussetzen"), 0 = keiner
  int wifiIndex;         // bevorzugtes WLAN aus WIFI_NETWORKS (0 = WLAN 1)
} cfg;

Preferences prefs;

const int SETTINGS_VERSION = 2;   // v2: Standard-Weckzeit 08:00 statt 05:30

void saveSettings();

void loadSettings() {
  prefs.begin("wecker", true);
  int version         = prefs.getInt("ver", 1);
  cfg.alarmHour       = prefs.getInt("hour", DEFAULT_ALARM_HOUR);
  cfg.alarmMinute     = prefs.getInt("min", DEFAULT_ALARM_MINUTE);
  cfg.enabled         = prefs.getBool("on", true);
  cfg.sunriseMinutes  = prefs.getInt("rise", DEFAULT_SUNRISE_MINUTES);
  cfg.daylightMinutes = prefs.getInt("day", DEFAULT_DAYLIGHT_MINUTES);
  cfg.maxBrightness   = prefs.getInt("bright", DEFAULT_MAX_BRIGHTNESS);
  cfg.skipStart       = (time_t)prefs.getLong64("skip", 0);
  cfg.wifiIndex       = constrain(prefs.getInt("wifi", 0), 0, WIFI_COUNT - 1);
  prefs.end();

  // Einmalig: gespeicherte Weckzeit aus der Zeit vor v2 auf den neuen Standard 08:00 setzen
  if (version < SETTINGS_VERSION) {
    cfg.alarmHour = DEFAULT_ALARM_HOUR;
    cfg.alarmMinute = DEFAULT_ALARM_MINUTE;
    saveSettings();
  }
}

void saveSettings() {
  prefs.begin("wecker", false);
  prefs.putInt("hour", cfg.alarmHour);
  prefs.putInt("min", cfg.alarmMinute);
  prefs.putBool("on", cfg.enabled);
  prefs.putInt("rise", cfg.sunriseMinutes);
  prefs.putInt("day", cfg.daylightMinutes);
  prefs.putInt("bright", cfg.maxBrightness);
  prefs.putLong64("skip", (int64_t)cfg.skipStart);
  prefs.putInt("wifi", cfg.wifiIndex);
  prefs.putInt("ver", SETTINGS_VERSION);
  prefs.end();
  Serial.printf("[CFG] Gespeichert: %02d:%02d, %s, Sonnenaufgang %d min, Licht danach %d min, Helligkeit %d, WLAN %d\n",
                cfg.alarmHour, cfg.alarmMinute, cfg.enabled ? "an" : "aus",
                cfg.sunriseMinutes, cfg.daylightMinutes, cfg.maxBrightness, cfg.wifiIndex + 1);
}

// ==========================================
// DRIFT-PROTOKOLL (Test: wie genau läuft die interne Uhr im Deep Sleep?)
// ==========================================

int loadDriftLog(DriftEntry *entries) {
  prefs.begin("drift", true);
  int count = min((int)(prefs.getBytesLength("log") / sizeof(DriftEntry)), DRIFT_LOG_SIZE);
  if (count > 0) prefs.getBytes("log", entries, count * sizeof(DriftEntry));
  prefs.end();
  return count;
}

// Neueste Messung hinten anhängen, bei vollem Protokoll fällt die älteste heraus
void appendDriftLog(const DriftEntry &entry) {
  DriftEntry entries[DRIFT_LOG_SIZE];
  int count = loadDriftLog(entries);
  if (count == DRIFT_LOG_SIZE) {
    memmove(entries, entries + 1, (DRIFT_LOG_SIZE - 1) * sizeof(DriftEntry));
    count--;
  }
  entries[count++] = entry;
  prefs.begin("drift", false);
  prefs.putBytes("log", entries, count * sizeof(DriftEntry));
  prefs.end();
}

void clearDriftLog() {
  prefs.begin("drift", false);
  prefs.remove("log");
  prefs.end();
}

// Abweichung hochgerechnet auf 8 Stunden Schlaf, in Sekunden
float driftPer8h(const DriftEntry &e) {
  return e.elapsedSec > 0 ? (e.offsetMs / 1000.0f) * (8 * 3600.0f / e.elapsedSec) : 0;
}

void printDriftLog() {
  DriftEntry entries[DRIFT_LOG_SIZE];
  int count = loadDriftLog(entries);
  Serial.printf("[DRIFT] %d Messungen (Abweichung > 0 = interne Uhr ging nach)\n", count);
  for (int i = 0; i < count; i++) {
    time_t t = (time_t)entries[i].syncTime;
    struct tm tm;
    localtime_r(&t, &tm);
    Serial.printf("[DRIFT] %02d.%02d. %02d:%02d  nach %5.1f h: %+7.1f s  (%+6.1f s pro 8 h)\n",
                  tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min,
                  entries[i].elapsedSec / 3600.0f, entries[i].offsetMs / 1000.0f, driftPer8h(entries[i]));
  }
}

// Zu spät aufgewacht: bis zum Ende des Sonnenaufgangs wird noch passend eingestiegen
int lateGraceSec() {
  return cfg.sunriseMinutes * 60;
}

// ==========================================
// INITIALISIERUNG
// ==========================================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

volatile bool ntpSynced = false;
volatile unsigned long ntpSyncMillis = 0;   // millis() beim Eintreffen der NTP-Zeit
struct timeval ntpSyncTime;                 // die neue Zeit von NTP

// Stand der internen Uhr beim Start der NTP-Abfrage (für die Drift-Messung)
struct timeval clockBeforeSync;
unsigned long clockBeforeSyncMillis = 0;
bool clockBeforeSyncValid = false;

int connectedWifi = -1;   // Index des verbundenen WLANs, -1 = keins

void onNtpSync(struct timeval *tv) {
  ntpSyncTime = *tv;
  ntpSyncMillis = millis();
  ntpSynced = true;
}

// ==========================================
// LEDS
// ==========================================

// Farbe für eine Helligkeit level = 0..1 (wahrgenommene Helligkeit),
// gammakorrigiert und auf die eingestellte Maximalhelligkeit begrenzt. Kein setBrightness(),
// da das bei kleinen Werten Farbinformation verliert.
uint32_t scaledColor(uint8_t r, uint8_t g, uint8_t b, float level) {
  level = constrain(level, 0.0f, 1.0f);
  float scale = powf(level, 2.2f) * cfg.maxBrightness / 255.0f;
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
// ZEIT & WLAN
// ==========================================

// Systemzeit gilt als gültig, wenn sie nach 2024 liegt (nach Stromausfall startet sie bei 1970)
bool systemTimeValid() {
  return time(nullptr) > 1704067200;
}

// showProgress: Die Ringe füllen sich grün, während verbunden wird
// (bis 6 LEDs beim WLAN-Verbinden, 7 = WLAN verbunden, 8 = Uhrzeit per NTP da).
// Erst das gewählte WLAN, bei Fehlschlag die anderen aus WIFI_NETWORKS.
bool connectWiFi(bool showProgress) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  connectedWifi = -1;

  int step = 0;   // für die Fortschrittsanzeige über alle Versuche hinweg
  for (int attempt = 0; attempt < WIFI_COUNT && connectedWifi < 0; attempt++) {
    int idx = (cfg.wifiIndex + attempt) % WIFI_COUNT;
    Serial.printf("[WIFI] Verbinde mit WLAN %d (%s)...\n", idx + 1, WIFI_NETWORKS[idx][0]);
    WiFi.disconnect();
    WiFi.begin(WIFI_NETWORKS[idx][0], WIFI_NETWORKS[idx][1]);

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 30) {
      if (showProgress) showRings(min(6, step / 4 + 1), 0, 255, 0, 0.5f);  // +1 LED alle 2 s
      delay(500);
      Serial.print(".");
      timeout++;
      step++;
    }
    if (WiFi.status() == WL_CONNECTED) connectedWifi = idx;
    else Serial.println();
  }

  bool ok = connectedWifi >= 0;
  if (ok) Serial.printf("\n[WIFI] Verbunden mit WLAN %d, IP %s\n", connectedWifi + 1, WiFi.localIP().toString().c_str());
  if (ok && showProgress) showRings(7, 0, 255, 0, 0.5f);
  return ok;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// NTP-Abfrage starten. Das Ergebnis meldet onNtpSync().
void startNtp() {
  ntpSynced = false;
  gettimeofday(&clockBeforeSync, nullptr);
  clockBeforeSyncMillis = millis();
  clockBeforeSyncValid = systemTimeValid();
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(TIME_ZONE, NTP_SERVER);
}

// Nach erfolgreichem NTP-Abgleich: Abweichung der internen Uhr messen und ggf. protokollieren
void recordClockDrift() {
  if (clockBeforeSyncValid && rtcLastSyncEpoch != 0) {
    // Was die interne Uhr im Moment der NTP-Antwort angezeigt hätte
    int64_t internalMs = (int64_t)clockBeforeSync.tv_sec * 1000 + clockBeforeSync.tv_usec / 1000
                       + (int64_t)(ntpSyncMillis - clockBeforeSyncMillis);
    int64_t ntpMs = (int64_t)ntpSyncTime.tv_sec * 1000 + ntpSyncTime.tv_usec / 1000;

    DriftEntry e;
    e.syncTime = ntpSyncTime.tv_sec;
    e.elapsedSec = (int32_t)(internalMs / 1000 - rtcLastSyncEpoch);
    e.offsetMs = (int32_t)(ntpMs - internalMs);
    Serial.printf("[DRIFT] Abweichung %+.1f s nach %.1f h ohne Abgleich\n", e.offsetMs / 1000.0f, e.elapsedSec / 3600.0f);
    if (e.elapsedSec >= DRIFT_LOG_MIN_SEC) appendDriftLog(e);
  }
  rtcLastSyncEpoch = ntpSyncTime.tv_sec;
}

bool syncTimeWithWiFi(bool showProgress) {
  bool ok = false;
  if (connectWiFi(showProgress)) {
    Serial.println("\n[NTP] Hole Uhrzeit...");
    startNtp();

    // Auf die echte NTP-Antwort warten. getLocalTime() reicht nicht: Die Systemzeit
    // läuft im Deep Sleep weiter und ist deshalb schon vor dem Sync "gültig".
    int retry = 0;
    while (!ntpSynced && retry < 20) {
      delay(500);
      retry++;
    }
    ok = ntpSynced;
    if (ok) recordClockDrift();
  }

  wifiOff();

  if (showProgress && ok) {
    showRings(LEDS_PER_RING, 0, 255, 0, 0.5f);   // Ringe voll grün: alles verbunden
    delay(1000);
  }
  if (!ok) Serial.println("\n[FEHLER] WLAN/NTP-Verbindung fehlgeschlagen.");
  return ok;
}

TimeSource getValidTime(bool showProgress) {
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

// Erster Sonnenaufgangs-Start (Weckzeit minus Sonnenaufgangsdauer) zum Zeitpunkt t oder danach.
// mktime() normalisiert Tages-/Monatsüberläufe und Sommerzeit (tm_isdst = -1).
time_t sunriseStartAtOrAfter(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  tm.tm_hour = cfg.alarmHour;
  tm.tm_min = cfg.alarmMinute - cfg.sunriseMinutes;
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

// Nächster Sonnenaufgang, der wirklich stattfindet: 0 = Wecker aus, ausgesetzter Tag wird übersprungen
time_t nextAlarmStart(time_t t) {
  if (!cfg.enabled) return 0;
  time_t start = sunriseStartAtOrAfter(t);
  if (start == cfg.skipStart) start = sunriseStartAtOrAfter(start + 1);
  return start;
}

// Wie nextAlarmStart, berücksichtigt aber einen anstehenden Probelauf
time_t nextSunriseStart(time_t t) {
  time_t start = nextAlarmStart(t);
  if (rtcTrialStart >= t && (start == 0 || rtcTrialStart < start)) {
    start = rtcTrialStart;
  }
  return start;
}

// ==========================================
// DEEP SLEEP
// ==========================================

// sleepSeconds = 0: ohne Timer schlafen, nur RESET weckt wieder (Wecker aus)
void goToDeepSleep(uint64_t sleepSeconds) {
  Serial.println("==========================================");
  if (sleepSeconds > 0) {
    Serial.printf("[SLEEP] Gehe fuer %llu Sekunden (%.1f Std) in Deep Sleep...\n",
                  sleepSeconds, sleepSeconds / 3600.0f);
  } else {
    Serial.println("[SLEEP] Wecker aus: Deep Sleep bis zum naechsten RESET.");
  }
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
  if (sleepSeconds > 0) {
    esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  }

  // Aufwachen per Taste (LOW). Klappt auf dem ESP32-C3 nur an GPIO 0-5, an GPIO 9
  // meldet die IDF sonst "gpio 9 is an invalid deep sleep wakeup IO".
  if ((1ULL << BUTTON_PIN) & SOC_GPIO_DEEP_SLEEP_WAKE_VALID_GPIO_MASK) {
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  esp_deep_sleep_start();
}

void goToDeepSleepUntilNextSunrise() {
  time_t now = time(nullptr);
  time_t start = nextSunriseStart(now + WAKE_MARGIN_SEC + 1);
  if (start == 0) {
    goToDeepSleep(0);
  }
  time_t wakeAt = start - WAKE_MARGIN_SEC;
  printTime("[SLEEP] Naechster Sonnenaufgang ab", start);

  uint64_t sleepSeconds = wakeAt > now ? wakeAt - now : 1;
  if (DRIFT_CHECK_ENABLED && sleepSeconds > DRIFT_CHECK_MIN_SEC) {
    // Zwischenstopp: Selbst bei 10 % Drift wacht der Chip so noch vor dem Sonnenaufgang auf,
    // holt die Uhrzeit neu und schläft den kurzen Rest dann genau. Die LEDs bleiben dabei aus.
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
// Ein Tastendruck beendet den Wecker jederzeit.
void runSunriseAlarm(unsigned long startOffsetMs, bool fast) {
  Serial.printf("\n[ALARM] >>> SONNENAUFGANG GESTARTET%s <<<\n", fast ? " (Zeitraffer)" : "");
  unsigned long startMillis = millis() - startOffsetMs;
  int currentPhase = -1;
  const float S = cfg.sunriseMinutes;

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
    else if (elapsedMinutes < S + cfg.daylightMinutes) {
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
// STATUS
// ==========================================

// true, wenn morgen früh kein Wecker klingelt (Wecker aus oder nächster Termin ausgesetzt)
bool noAlarmNext(time_t now) {
  return !cfg.enabled || sunriseStartAtOrAfter(now) == cfg.skipStart;
}

// Anzeige nach jedem Reset, direkt nach dem grünen Füllen beim WLAN-Verbinden:
//   gelb  = kein WLAN, interne Uhr läuft     rot blinkend = keine Uhrzeit, Wecker geht nicht
//   rot leuchtend = Wecker aus bzw. nächster Wecker ausgesetzt
//   sonst orange = volle Stunden bis zur Weckzeit (1 LED = 1 h, erst Ring 1, dann Ring 2)
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
  if (noAlarmNext(now)) {
    Serial.println(cfg.enabled ? "[STATUS] Naechster Wecker ausgesetzt." : "[STATUS] Wecker ist aus.");
    clearLEDs();
    delay(300);
    showRings(LEDS_PER_RING, 255, 0, 0, 0.5f);   // rot leuchtend: morgen kein Wecker
    delay(3000);
    clearLEDs();
    return;
  }

  time_t alarm = sunriseStartAtOrAfter(now) + cfg.sunriseMinutes * 60;
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

// ==========================================
// EINSTELLUNGSSEITE (Menü-Modus 4)
// ==========================================

WebAction webAction = WEB_NONE;
unsigned long webLastRequest = 0;

String twoDigits(int v) {
  return v < 10 ? "0" + String(v) : String(v);
}

String formatDateTime(time_t t) {
  static const char *WEEKDAYS[] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
  struct tm tm;
  localtime_r(&t, &tm);
  return String(WEEKDAYS[tm.tm_wday]) + " " + twoDigits(tm.tm_hour) + ":" + twoDigits(tm.tm_min);
}

String settingsPage(const String &message) {
  time_t now = time(nullptr);
  bool timeOk = systemTimeValid();

  String status;
  if (!timeOk) {
    status = "Uhrzeit wird noch geholt, Seite gleich neu laden.";
  } else if (!cfg.enabled) {
    status = "Jetzt: " + formatDateTime(now) + "<br><b>Wecker ist aus.</b>";
  } else {
    time_t start = nextAlarmStart(now);
    status = "Jetzt: " + formatDateTime(now) +
             "<br>N&auml;chster Wecker: <b>" + formatDateTime(start + cfg.sunriseMinutes * 60) + "</b>" +
             " (Sonnenaufgang ab " + formatDateTime(start).substring(3) + ")";
    if (sunriseStartAtOrAfter(now) == cfg.skipStart) status += "<br>Der Termin davor ist ausgesetzt.";
  }
  bool skipChecked = timeOk && sunriseStartAtOrAfter(now) == cfg.skipStart;

  String h;
  h.reserve(4000);
  h += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Lichtwecker</title><style>"
         ":root{color-scheme:light dark;--bg:#fff;--fg:#1d1d1f;--card:#f2f2f5;--acc:#e8702a}"
         "@media(prefers-color-scheme:dark){:root{--bg:#111;--fg:#eee;--card:#222}}"
         "body{font:17px -apple-system,system-ui,sans-serif;background:var(--bg);color:var(--fg);margin:0 auto;max-width:480px;padding:16px}"
         "h1{font-size:24px;margin:8px 0 16px}.card{background:var(--card);border-radius:14px;padding:14px 16px;margin-bottom:14px}"
         "label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0}"
         "input[type=time],input[type=number]{font-size:17px;padding:6px 8px;border-radius:8px;border:1px solid #888;background:var(--bg);color:var(--fg);width:7em}"
         "input[type=checkbox]{width:22px;height:22px}"
         "button{width:100%;font-size:17px;padding:12px;border:0;border-radius:12px;margin-top:8px;background:var(--card);color:var(--fg)}"
         "button.main{background:var(--acc);color:#fff;font-weight:600}.msg{background:#2a7;color:#fff;padding:10px 14px;border-radius:12px}"
         "small{opacity:.7}</style></head><body><h1>Lichtwecker</h1>");
  if (message.length() > 0) h += "<p class='msg'>" + message + "</p>";
  h += "<div class='card'>" + status + "</div>";

  h += F("<form method='post' action='/save'><div class='card'>");
  h += "<label>Weckzeit <input type='time' name='t' required value='" +
       twoDigits(cfg.alarmHour) + ":" + twoDigits(cfg.alarmMinute) + "'></label>";
  h += String("<label>Wecker an <input type='checkbox' name='on'") + (cfg.enabled ? " checked" : "") + "></label>";
  h += String("<label>N&auml;chsten Wecker aussetzen <input type='checkbox' name='skip'") + (skipChecked ? " checked" : "") + "></label>";
  h += F("</div><div class='card'>");
  h += "<label>Sonnenaufgang (min) <input type='number' name='rise' min='5' max='60' value='" + String(cfg.sunriseMinutes) + "'></label>";
  h += "<label>Licht nach Weckzeit (min) <input type='number' name='day' min='0' max='60' value='" + String(cfg.daylightMinutes) + "'></label>";
  h += "<label>Max. Helligkeit <input type='number' name='bright' min='10' max='150' value='" + String(cfg.maxBrightness) + "'></label>";
  h += F("<small>Helligkeit 10&ndash;150, Standard 76 (&asymp; 30 %). Heller = mehr Strom und W&auml;rme am Auge.</small>");
  h += F("</div><div class='card'><label>WLAN <select name='wifi' style='font-size:17px'>");
  for (int i = 0; i < WIFI_COUNT; i++) {
    h += "<option value='" + String(i) + "'" + (i == cfg.wifiIndex ? " selected" : "") + ">" +
         String(i + 1) + ": " + WIFI_NETWORKS[i][0] + "</option>";
  }
  h += F("</select></label><small>Wird zuerst benutzt, bei Fehlschlag automatisch das andere.</small>");
  h += F("</div><button class='main' type='submit'>Speichern</button></form>");

  h += driftLogHtml();

  h += F("<form method='post' action='/action'><div class='card'>"
         "<button name='a' value='preview'>Sonnenaufgang-Vorschau (1 min)</button>"
         "<button name='a' value='led'>LED-Test</button>"
         "<button name='a' value='done'>Fertig (Maske geht schlafen)</button>"
         "</div></form><small>Die Seite ist bis zu 5 Minuten nach dem letzten Aufruf erreichbar. "
         "BOOT-Taste an der Maske beendet sie sofort.</small></body></html>");
  return h;
}

// Drift-Protokoll als Tabelle, neueste Messung oben
String driftLogHtml() {
  DriftEntry entries[DRIFT_LOG_SIZE];
  int count = loadDriftLog(entries);

  String h = F("<div class='card'><b>Uhr-Abweichung (Test)</b><br>");
  if (count == 0) {
    h += F("<small>Noch keine Messung. Gemessen wird beim Aufwachen morgens, wenn die Maske ihre Uhr per WLAN abgleicht.</small>");
  } else {
    float sum = 0;
    h += F("<table style='width:100%;font-size:15px;margin-top:8px'><tr><th align=left>Abgleich</th>"
           "<th align=right>nach</th><th align=right>Abw.</th><th align=right>pro 8 h</th></tr>");
    for (int i = count - 1; i >= 0; i--) {
      time_t t = (time_t)entries[i].syncTime;
      struct tm tm;
      localtime_r(&t, &tm);
      float per8h = driftPer8h(entries[i]);
      sum += per8h;
      h += "<tr><td>" + twoDigits(tm.tm_mday) + "." + twoDigits(tm.tm_mon + 1) + ". " +
           twoDigits(tm.tm_hour) + ":" + twoDigits(tm.tm_min) + "</td><td align=right>" +
           String(entries[i].elapsedSec / 3600.0f, 1) + " h</td><td align=right>" +
           String(entries[i].offsetMs / 1000.0f, 1) + " s</td><td align=right>" +
           String(per8h, 1) + " s</td></tr>";
    }
    h += "</table><small>Mittel: " + String(sum / count, 1) + " s pro 8 h. Plus = interne Uhr ging nach "
         "(Wecker w&auml;re zu sp&auml;t gekommen).</small>";
    h += F("<form method='post' action='/action'><button name='a' value='clearlog'>Protokoll l&ouml;schen</button></form>");
  }
  h += F("</div>");
  return h;
}

void handleRoot() {
  webLastRequest = millis();
  server.send(200, "text/html; charset=utf-8", settingsPage(server.hasArg("saved") ? "Gespeichert." : ""));
}

void handleSave() {
  webLastRequest = millis();
  int hh, mm;
  if (sscanf(server.arg("t").c_str(), "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh < 24 && mm >= 0 && mm < 60) {
    cfg.alarmHour = hh;
    cfg.alarmMinute = mm;
  }
  cfg.enabled = server.hasArg("on");
  if (server.hasArg("rise"))   cfg.sunriseMinutes  = constrain(server.arg("rise").toInt(), 5, 60);
  if (server.hasArg("day"))    cfg.daylightMinutes = constrain(server.arg("day").toInt(), 0, 60);
  if (server.hasArg("bright")) cfg.maxBrightness   = constrain(server.arg("bright").toInt(), 10, 150);
  if (server.hasArg("wifi"))   cfg.wifiIndex       = constrain(server.arg("wifi").toInt(), 0, WIFI_COUNT - 1);
  // Aussetzen bezieht sich auf den nächsten Termin mit der gerade gespeicherten Weckzeit
  cfg.skipStart = (server.hasArg("skip") && systemTimeValid()) ? sunriseStartAtOrAfter(time(nullptr)) : 0;
  saveSettings();
  blink(0, 255, 0, 1);   // Bestätigung an der Maske

  server.sendHeader("Location", "/?saved=1");
  server.send(303);
}

void handleAction() {
  webLastRequest = millis();
  String a = server.arg("a");
  String msg;
  if (a == "led")          { webAction = WEB_LED_TEST; msg = "LED-Test l&auml;uft (ca. 16 s)."; }
  else if (a == "preview") { webAction = WEB_PREVIEW;  msg = "Vorschau l&auml;uft (ca. 1 min). BOOT-Taste bricht ab."; }
  else if (a == "done")    { webAction = WEB_DONE;     msg = "Fertig. Die Maske zeigt den Status und schl&auml;ft dann."; }
  else if (a == "clearlog") { clearDriftLog();         msg = "Drift-Protokoll gel&ouml;scht."; }
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<body style='font:17px -apple-system,system-ui,sans-serif;padding:16px'><p>" + msg +
              "</p><p><a href='/'>Zur&uuml;ck zu den Einstellungen</a></p></body>");
}

// Captive Portal: Jede unbekannte Adresse (z. B. Apples WLAN-Prüfung captive.apple.com)
// wird auf die Einstellungsseite umgeleitet. Dadurch öffnet das iPhone die Seite von selbst.
void handleCaptive() {
  webLastRequest = millis();
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  server.send(302);
}

// Die Seite ist auf zwei Wegen erreichbar:
//  1. eigenes WLAN "Lichtwecker" (offen): am iPhone verbinden, die Seite öffnet sich automatisch
//     (sonst http://192.168.4.1). Funktioniert unabhängig von Router und Repeater.
//  2. im Heim-WLAN unter http://lichtwecker.local (klappt nicht mit jedem Repeater)
// Ringe "atmen" blau, solange die Seite erreichbar ist.
// Ende: "Fertig" auf der Seite, BOOT-Taste oder 5 min ohne Aufruf.
void runSettingsServer() {
  Serial.println("[WEB] Einstellungen: verbinde WLAN...");
  bool homeWifi = connectWiFi(true);   // für Uhrzeit und lichtwecker.local, nicht zwingend nötig
  if (!homeWifi) Serial.println("\n[WEB] Kein Heim-WLAN, nur eigenes WLAN 'Lichtwecker'.");

  WiFi.mode(homeWifi ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);   // Funk-Stromsparen aus, sonst gehen mDNS-Anfragen verloren
  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());

  bool ntpRecorded = true;
  if (homeWifi) {
    startNtp();   // Uhrzeit für die Seite, läuft im Hintergrund
    ntpRecorded = false;
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/action", HTTP_POST, handleAction);
  server.onNotFound(handleCaptive);
  server.begin();
  Serial.printf("[WEB] Eigenes WLAN '%s': http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  if (homeWifi) Serial.printf("[WEB] Heim-WLAN: http://%s.local  bzw. http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());

  webLastRequest = millis();
  webAction = WEB_NONE;
  unsigned long lastLedUpdate = 0;
  while (millis() - webLastRequest < WEB_TIMEOUT_MS) {
    dns.processNextRequest();
    server.handleClient();

    if (!ntpRecorded && ntpSynced) {
      recordClockDrift();
      ntpRecorded = true;
    }

    if (webAction == WEB_DONE) break;
    if (webAction == WEB_LED_TEST) { webAction = WEB_NONE; runLedTest(); webLastRequest = millis(); }
    if (webAction == WEB_PREVIEW)  { webAction = WEB_NONE; runSunriseAlarm(0, true); webLastRequest = millis(); }

    if (buttonPressed()) {
      waitForRelease();
      break;
    }

    if (millis() - lastLedUpdate >= 40) {
      showRings(LEDS_PER_RING, 0, 80, 255, 0.3f + 0.15f * sinf(millis() / 600.0f));
      lastLedUpdate = millis();
    }
    delay(2);
  }

  server.stop();
  dns.stop();
  if (homeWifi) MDNS.end();
  WiFi.softAPdisconnect(true);
  wifiOff();
  clearLEDs();
  Serial.println("[WEB] Einstellungen beendet.");
}

// ==========================================
// TESTMENÜ
// ==========================================

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

// Modus 5: Wecker an/aus umschalten, neuer Zustand 2 s lang grün (an) oder rot (aus)
void toggleAlarmEnabled() {
  cfg.enabled = !cfg.enabled;
  saveSettings();
  if (cfg.enabled) showRings(LEDS_PER_RING, 0, 255, 0, 0.5f);
  else             showRings(LEDS_PER_RING, 255, 0, 0, 0.5f);
  delay(2000);
  clearLEDs();
}

// Modus 6: bevorzugtes WLAN wechseln, danach 2 s lang so viele grüne LEDs wie die Nummer des WLANs
void switchWifi() {
  cfg.wifiIndex = (cfg.wifiIndex + 1) % WIFI_COUNT;
  saveSettings();
  Serial.printf("[MENU] Bevorzugtes WLAN: %d (%s)\n", cfg.wifiIndex + 1, WIFI_NETWORKS[cfg.wifiIndex][0]);
  showRings(cfg.wifiIndex + 1, 0, 255, 0, 0.5f);
  delay(2000);
  clearLEDs();
}

// Testmenü: Modus = Anzahl hellblauer LEDs. Kurz drücken = nächster Modus, lang = starten.
MenuResult runTestMenu() {
  const int MODE_COUNT = 6;
  int mode = 1;
  Serial.println("[MENU] 1 = LED-Test, 2 = Zeitraffer, 3 = Probelauf, 4 = Einstellungen, 5 = Wecker an/aus, 6 = WLAN wechseln");
  blink(0, 180, 255, 2);   // Menü erkannt

  while (true) {
    showCount(mode, 0, 180, 255, 0.5f);
    Serial.printf("[MENU] Modus %d\n", mode);

    ButtonEvent ev = waitForButton(MENU_TIMEOUT_MS);
    if (ev == BTN_NONE) {
      clearLEDs();
      Serial.println("[MENU] Timeout, zurueck in den Normalbetrieb.");
      return MENU_NORMAL;
    }
    if (ev == BTN_SHORT) {
      mode = mode % MODE_COUNT + 1;
      continue;
    }

    switch (mode) {
      case 1: runLedTest(); break;
      case 2: runSunriseAlarm(0, true); break;
      case 3: return MENU_TRIAL;
      case 4: runSettingsServer(); return MENU_NORMAL;
      case 5: toggleAlarmEnabled(); break;
      case 6: switchWifi(); break;
    }
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

  // Zeitzone immer setzen, auch wenn WLAN ausfällt, sonst rechnet localtime in UTC
  setenv("TZ", TIME_ZONE, 1);
  tzset();
  loadSettings();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  ledPower(true);
  strip.begin();
  clearLEDs();

  // Nach Reset/Einschalten: 3 s Fenster für USB-Uploads und fürs Testmenü.
  // Die erste LED leuchtet blau, solange man BOOT drücken kann.
  // (BOOT nicht schon beim Reset halten, sonst startet der Chip im Download-Modus.)
  // Beim nächtlichen Aufwachen (Timer) bleiben alle LEDs aus.
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
  if (afterReset) printDriftLog();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[WAKEUP] Durch Manuelle Taste aufgeweckt!");
  }

  bool trialRequested = menuRequested && runTestMenu() == MENU_TRIAL;

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
  // lateGraceSec() hätte beginnen sollen). 0 = keiner.
  bool trial = rtcTrialStart != 0 && now <= rtcTrialStart + lateGraceSec();
  if (!trial) rtcTrialStart = 0;
  time_t start = 0;
  if (trial) start = rtcTrialStart;
  else if (cfg.enabled) start = sunriseStartAtOrAfter(now - lateGraceSec());

  if (start == 0) {
    Serial.println("[ALARM] Wecker ist aus.");
  }
  else if (!trial && start == cfg.skipStart) {
    Serial.println("[ALARM] Dieser Wecker ist ausgesetzt.");
    if (start - now <= WAKE_MARGIN_SEC) {
      cfg.skipStart = 0;   // erledigt, ab morgen wieder normal
      saveSettings();
    }
  }
  else if (start == rtcHandledSunrise) {
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
