// Vorlage: nach secrets.h kopieren und ausfüllen.
// secrets.h steht in .gitignore und wird nicht committet.
#pragma once

// WLAN 1 ist Standard. Umschalten: Menü-Modus 6 oder Einstellungsseite.
// Ist das gewählte WLAN nicht erreichbar, wird automatisch das andere probiert.
const char* WIFI_NETWORKS[][2] = {
  { "WLAN_1", "PASSWORT_1" },
  { "WLAN_2", "PASSWORT_2" },
};
