#include "wifi_manager.h"
#include <ESPmDNS.h>

namespace wifimgr {

const char* statusName(wl_status_t s)
{
    switch (s) {
        case WL_NO_SHIELD:       return "NO_SHIELD";
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

/*
 * Index of `ssid` within the last scan, or -1.
 *
 * Matching is CASE-INSENSITIVE by deliberate choice. 802.11 SSIDs are opaque
 * byte strings and are technically case-sensitive, so an exact-match scan
 * lookup is "correct" -- and it cost us a debugging session: the iPhone
 * hotspot broadcasts "RY-OPS" while iOS displays the device name as "ry-ops".
 * Apple upper-cases the beacon SSID; the UI does not. An exact match therefore
 * reports "not in scan" for a network sitting at -24 dBm.
 *
 * Case-folding here only affects which scan entry we *select*. The passphrase
 * is unchanged, and WiFi.begin() is handed the SSID exactly as the AP
 * broadcasts it (WiFi.SSID(idx)), not as the user typed it -- so the
 * association itself remains byte-exact.
 */
static bool isOpenProfile(const Profile& p)
{
    return !p.password || !*p.password;
}

static int findInScan(int n, const char* ssid)
{
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) == ssid) return i;            // exact wins
    }
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i).equalsIgnoreCase(ssid)) {     // then case-folded
            log_w("wifi: '%s' matched broadcast '%s' (case differs)",
                  ssid, WiFi.SSID(i).c_str());
            return i;
        }
    }
    return -1;
}

// Start the mDNS responder and advertise the mirror's HTTP service, so the
// device is reachable as <hostname>.local regardless of which DHCP lease it
// happens to get. Advertising _http._tcp is what makes it show up in network
// browsers rather than only resolving by name.
static bool startMdns(const char* hostname)
{
    if (!hostname || !*hostname) return false;
    if (!MDNS.begin(hostname)) {
        log_w("wifi: mDNS responder failed to start");
        return false;
    }
    MDNS.addService("http", "tcp", 80);
    log_i("wifi: mDNS up -> http://%s.local", hostname);
    return true;
}

// Rescan budget for a dormant hotspot. 4 passes x 3.5 s of settle time.
static constexpr int      kScanPasses    = 4;
static constexpr uint32_t kRescanDelayMs = 3500;

Result begin(const Profile* profiles, size_t count,
             const char* apSsid, const char* apPass,
             const char* hostname, uint32_t perProfileMs)
{
    Result r;
    const uint32_t t0 = millis();

    r.hostname = hostname;

    WiFi.mode(WIFI_STA);
    // Must precede WiFi.begin(): the hostname is carried in the DHCP DISCOVER,
    // so setting it after association leaves the router showing "espressif".
    if (hostname && *hostname) WiFi.setHostname(hostname);
    WiFi.persistent(false);      // don't wear flash rewriting creds each boot
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(true, true); // clear any stored AP from a prior sketch
    delay(100);

    // An iPhone Personal Hotspot powers its radio down when nothing is joined
    // to it, and only wakes when the owner opens the Settings pane or another
    // client appears. A single scan at boot therefore misses it entirely --
    // which is exactly how this device ended up on its own SoftAP while the
    // display cheerfully advertised a name nobody could resolve. Rescan a few
    // times before giving up; the whole window is ~15 s, which is much cheaper
    // than a fallback nobody notices.
    for (int pass = 0; pass < kScanPasses; ++pass) {
    if (pass) {
        log_w("wifi: rescan pass %d/%d (an idle iPhone hotspot needs a moment)",
              pass + 1, kScanPasses);
        delay(kRescanDelayMs);
    }

    // Scan first. The ESP32-S3 radio is 2.4 GHz only, so anything missing here
    // is either 5 GHz, asleep, or out of range — all worth distinguishing from
    // a wrong passphrase.
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    r.scanCount = n;

    // Dump every SSID the radio can actually see. "not in scan" is ambiguous
    // on its own: it can mean 5 GHz-only, asleep, out of range -- or an SSID
    // that differs from the configured one by case or an invisible character.
    // SSID comparison is byte-exact, so printing the truth ends the guessing.
    // Hex is included because iOS device names routinely carry a curly
    // apostrophe (U+2019) or an emoji that looks like nothing in a terminal.
    // Full dump on the first pass only -- it is the diagnostic that matters and
    // repeating 20+ lines every rescan buries it.
    if (pass == 0) {
    Serial.printf("\n--- WiFi scan: %d networks (2.4 GHz only; this radio cannot see 5 GHz) ---\n", n);
    for (int i = 0; i < n; ++i) {
        const String s = WiFi.SSID(i);
        Serial.printf("  [%2d] ch%-3d %4ddBm  \"%s\"", i, WiFi.channel(i), WiFi.RSSI(i), s.c_str());
        bool nonAscii = false;
        for (size_t k = 0; k < s.length(); ++k)
            if ((uint8_t)s[k] < 0x20 || (uint8_t)s[k] > 0x7E) nonAscii = true;
        if (s.length() == 0) {
            Serial.print("   <hidden>");
        } else if (nonAscii) {
            Serial.print("   hex=");
            for (size_t k = 0; k < s.length(); ++k) Serial.printf("%02X ", (uint8_t)s[k]);
        }
        Serial.println();
    }
    Serial.printf("--- looking for \"%s\" ---\n\n", count ? profiles[0].ssid : "(none)");
    } else {
        Serial.printf("  rescan %d: %d networks, \"%s\" %s\n", pass + 1, n,
                      count ? profiles[0].ssid : "(none)",
                      (count && findInScan(n, profiles[0].ssid) >= 0) ? "FOUND" : "absent");
    }

    for (size_t p = 0; p < count; ++p) {
        const Profile& prof = profiles[p];
        if (!prof.ssid || !*prof.ssid) continue;

        const int idx = (n > 0) ? findInScan(n, prof.ssid) : -1;
        if (n > 0 && idx < 0) {
            // Not visible. Skip fast; do not burn perProfileMs on it.
            if (p == 0) {
                r.seenInScan = false;
                r.detail = "top profile not in scan (5 GHz-only, asleep, or out of range)";
            }
            log_w("wifi: '%s' not in scan (%d networks) - skipping", prof.ssid, n);
            continue;
        }
        if (p == 0) r.seenInScan = true;

        // Associate using the SSID exactly as broadcast, so a case-folded
        // match still produces a byte-exact association.
        // The framework refuses to associate below WPA2 by default:
        //   WiFiSTAClass::_minSecurity = WIFI_AUTH_WPA2_PSK
        // and that threshold is applied inside wifi_sta_config(). A WPA-PSK-only
        // or WEP network is therefore rejected BEFORE any air traffic, and the
        // failure surfaces as a plain association timeout with no hint of the
        // cause. Corporate IoT SSIDs are exactly where legacy auth still lives,
        // so lower the floor and let the scan's reported auth mode be the thing
        // that tells us what we are dealing with.
        WiFi.setMinSecurity(WIFI_AUTH_WEP);

        const String joinSsid = (idx >= 0) ? WiFi.SSID(idx) : String(prof.ssid);
        log_i("wifi: joining '%s'%s", joinSsid.c_str(),
              idx >= 0 ? "" : " (not scanned; trying blind)");
        // An open network needs a NULL passphrase, not "". Passing an empty
        // string still takes the password branch in wifi_sta_config() in some
        // paths; NULL is unambiguous.
        const char* pw = (prof.password && *prof.password) ? prof.password : nullptr;
        WiFi.begin(joinSsid.c_str(), pw);

        const uint32_t deadline = millis() + perProfileMs;
        while (millis() < deadline && WiFi.status() != WL_CONNECTED) delay(100);

        if (WiFi.status() == WL_CONNECTED) {
            r.outcome   = Outcome::Connected;
            // Report the configured name; the broadcast form is in the log.
            r.ssid      = prof.ssid;
            r.rssi      = WiFi.RSSI();
            r.channel   = WiFi.channel();
            r.elapsedMs = millis() - t0;
            r.detail    = "";
            r.mdnsUp    = startMdns(hostname);
            r.openNet   = isOpenProfile(prof);
            // An open corporate SSID very often sits behind a captive portal:
            // association succeeds, DHCP succeeds, and every outbound request
            // is intercepted until a browser completes a sign-on the ESP32
            // cannot perform. That state looks like a clean connection here, so
            // flag it rather than let it read as unqualified success.
            WiFi.scanDelete();
            log_i("wifi: connected to '%s' ip=%s rssi=%d ch=%d in %ums",
                  r.ssid, WiFi.localIP().toString().c_str(),
                  (int)r.rssi, (int)r.channel, r.elapsedMs);
            return r;
        }

        // Visible but refused. The right explanation depends on what the AP
        // advertises, and "check passphrase" is actively misleading on an open
        // or enterprise network -- there is no passphrase to check.
        const wl_status_t st = WiFi.status();
        const bool isOpen = !pw;
        const wifi_auth_mode_t am =
            (idx >= 0) ? WiFi.encryptionType(idx) : WIFI_AUTH_MAX;
        log_w("wifi: '%s' failed (%s, auth=%d, open_attempt=%d)",
              prof.ssid, statusName(st), (int)am, (int)isOpen);
        if (p == 0) {
            if (st == WL_NO_SSID_AVAIL)
                r.detail = "SSID vanished mid-connect";
            else if (am == WIFI_AUTH_ENTERPRISE || am == WIFI_AUTH_WPA3_ENT_192)
                r.detail = "802.1X enterprise - needs EAP identity, not a passphrase";
            else if (isOpen && am != WIFI_AUTH_OPEN)
                r.detail = "AP wants a passphrase but none is configured";
            else if (!isOpen && am == WIFI_AUTH_OPEN)
                r.detail = "AP is open but a passphrase is configured - clear it";
            else if (isOpen)
                r.detail = "open network refused association (MAC filter or AP limit?)";
            else
                r.detail = "visible but refused - check passphrase";
        }
        WiFi.disconnect(true, true);
        delay(100);
    }

    WiFi.scanDelete();
    }  // rescan pass

    // Nothing joined. SoftAP so the mirror is still reachable.
    WiFi.mode(WIFI_AP);
    const bool up = WiFi.softAP(apSsid, apPass);
    r.outcome   = up ? Outcome::SoftAP : Outcome::Failed;
    r.ssid      = up ? apSsid : nullptr;
    r.channel   = up ? WiFi.channel() : 0;
    // Works in AP mode as well, so the .local name is the one address that is
    // correct in both outcomes.
    if (up) r.mdnsUp = startMdns(hostname);
    r.elapsedMs = millis() - t0;
    if (!*r.detail) r.detail = "no profile joined";
    log_w("wifi: falling back to SoftAP '%s' (%s)", apSsid, r.detail);
    return r;
}

}  // namespace wifimgr
