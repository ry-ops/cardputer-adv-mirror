/*
 * wifi_credentials.example.h — TEMPLATE. Copy to wifi_credentials.h and edit.
 *
 *     cp include/wifi_credentials.example.h include/wifi_credentials.h
 *
 * wifi_credentials.h is gitignored so real passphrases never reach the repo.
 * The build fails with a readable #error if it is missing.
 *
 * ORDER MATTERS: profiles are tried top to bottom. Put the network you most
 * want on top. A profile whose SSID is not seen in the scan is skipped fast
 * rather than burning the full connect timeout.
 *
 * 2.4 GHz ONLY. The ESP32-S3 has no 5 GHz radio. A 5 GHz-only network is
 * invisible to it — it will not appear in the scan and cannot be joined.
 */
#pragma once

#define WIFI_PROFILES                       \
    { "your-network",   "your-password"   }, \
    { "backup-network", "backup-password" },

// SoftAP fallback, used when no profile connects. Always available.
#define WIFI_AP_SSID "CardputerADV"
#define WIFI_AP_PASS "cardputer"
