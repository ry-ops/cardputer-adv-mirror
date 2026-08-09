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
 *
 * SPELL THE SSID AS THE AP BROADCASTS IT, not as a phone's UI displays it.
 * iOS personal hotspots are the common trap: the Settings screen shows the
 * device name in mixed case ("ry-ops") while the beacon carries it upper-cased
 * ("RY-OPS"). findInScan() case-folds as a fallback so a mismatch still matches
 * a scan hit, but when the scan returns ZERO networks the join goes out blind
 * with the literal below, and 802.11 compares SSIDs as opaque bytes — a
 * mis-cased blind join cannot associate. Read the spelling off the Scan screen.
 */
#pragma once

#define WIFI_PROFILES                       \
    { "your-network",   "your-password"   }, \
    { "backup-network", "backup-password" },

// SoftAP fallback, used when no profile connects. Always available.
#define WIFI_AP_SSID "CardputerADV"
#define WIFI_AP_PASS "cardputer"
