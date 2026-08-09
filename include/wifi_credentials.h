/*
 * wifi_credentials.h — SSIDs and passwords below are REDACTED placeholders.
 *
 * Profiles are tried in order.
 *
 * TOP PROFILE: MDC(IoT) -- OPEN, no passphrase.
 *   Parentheses are perfectly legal in an SSID -- 802.11 treats the SSID as an
 *   opaque byte string, and this is a plain C string literal, so no escaping is
 *   needed. Do NOT "fix" the name by removing them.
 *
 *   If this is a corporate IoT network it may be 802.1X (enterprise) or open
 *   with a captive portal. Neither can be joined with SSID+passphrase alone.
 *   The Scan screen now prints the auth mode next to each network -- check it
 *   reads WPA2/WPA3/OPEN and not ENT before assuming the passphrase is wrong.
 *
 * RY-OPS is an iPhone personal hotspot, kept as a fallback profile.
 *
 * SPELLING — store the BROADCAST form, uppercase:
 *   iOS shows the device name as "ry-ops" but the beacon carries "RY-OPS".
 *   findInScan() case-folds as a fallback, so a lowercase entry still MATCHES a
 *   scan hit and associates byte-exactly (it joins with WiFi.SSID(idx), the
 *   received string). The lowercase spelling was therefore not the cause of the
 *   earlier join failures -- ADR 0014/0015 traced those to the scan screen
 *   comparing against the SoftAP's own name and to the verdict line printing a
 *   compile-time constant.
 *
 *   It still matters in one path: when the scan returns ZERO networks, idx is -1
 *   and begin() joins blind with this literal. 802.11 compares SSIDs as opaque
 *   bytes, so a blind join with "ry-ops" cannot associate. Storing the broadcast
 *   spelling also makes the exact-match branch win first and makes r.ssid --
 *   which is the configured string, not the received one -- print the truth.
 *
 * IMPORTANT — iPhone hotspot band:
 *   iOS defaults the hotspot to 5 GHz on recent models. The ESP32-S3 is
 *   2.4 GHz only and literally cannot see a 5 GHz network. If ry-ops does not
 *   appear in the scan (wifi_manager reports "not in scan"), enable
 *       Settings -> Personal Hotspot -> Maximize Compatibility
 *   which forces the hotspot to 2.4 GHz. Leave it on.
 *
 *   Also: iOS sleeps the hotspot when no client is attached. Keep the Personal
 *   Hotspot settings screen open while the device boots, or join it from the
 *   Mac first, so the radio is awake when the ESP32 scans.
 */
#pragma once

/*
 * NOTE ON COMMENTS: never put a // comment inside this macro. Line splicing
 * (translation phase 2) runs BEFORE comment removal (phase 3), so a // comment
 * on a backslash-continued line swallows the next line into the comment and the
 * macro silently loses profiles. This cost a build. Keep prose out here.
 *
 * MDC(IoT) has an EMPTY passphrase because it is an open network. The empty
 * string is converted to a NULL passphrase at the WiFi.begin() call site, which
 * is what actually selects open association. Do not use a placeholder string.
 */
#define WIFI_PROFILES                    \
    { "REDACTED_SSID_1", "" }, \
    { "REDACTED_SSID_2", "REDACTED_PASSWORD" },       \

// SoftAP fallback, used when no profile connects.
#define WIFI_AP_SSID "CardputerADV"
#define WIFI_AP_PASS "cardputer"
