#include "wifi_manager_rt.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

namespace wifimgr {
namespace {
const Profile* s_profiles = nullptr;
size_t         s_count    = 0;
const char*    s_apSsid   = nullptr;
const char*    s_apPass   = nullptr;
const char*    s_hostname = nullptr;
Result         s_last;

bool ieq(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
    }
    return !*a && !*b;
}
}  // namespace

void remember(const Profile* profiles, size_t count,
              const char* apSsid, const char* apPass, const char* hostname)
{
    s_profiles = profiles; s_count = count;
    s_apSsid = apSsid; s_apPass = apPass; s_hostname = hostname;
}

const char* authName(uint8_t a)
{
    switch (a) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_ENTERPRISE:      return "ENT";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        case WIFI_AUTH_WPA3_ENT_192:    return "ENT192";
        default:                        return "?";
    }
}

const char* targetSsid()
{
    return (s_profiles && s_count) ? s_profiles[0].ssid : "";
}

const Result& last()            { return s_last; }
void setLast(const Result& r)   { s_last = r; }

Result retry()
{
    if (!s_profiles || !s_count) return s_last;
    s_last = begin(s_profiles, s_count, s_apSsid, s_apPass, s_hostname);
    return s_last;
}

int scanOnce(ScanEntry* out, int max, const char* wanted)
{
    // Scanning from SoftAP is compromised: scanNetworks() calls enableSTA(true)
    // internally, so the radio ends up in AP+STA and must keep returning to the
    // AP's channel to beacon. Per-channel dwell drops and weak APs get missed,
    // which is exactly the false negative this screen must not produce.
    //
    // So drop the AP for the duration of a manual scan. This does disconnect a
    // browser client briefly -- acceptable, because the user explicitly asked
    // for a scan, and a scan that cannot be trusted is worth nothing.
    const wifi_mode_t prevMode = WiFi.getMode();
    const bool hadAP = (prevMode == WIFI_AP || prevMode == WIFI_AP_STA);
    if (hadAP) { WiFi.mode(WIFI_STA); delay(120); }

    // max_ms_per_chan=300 (default 120). An iPhone hotspot beacons every 100 ms
    // but its radio duty-cycles when idle; a short dwell can land entirely in a
    // quiet gap.
    const int n = WiFi.scanNetworks(false, /*show_hidden=*/true,
                                    /*passive=*/false, /*max_ms_per_chan=*/300);

    if (hadAP) { WiFi.mode(prevMode); delay(80); }
    if (n <= 0) { WiFi.scanDelete(); return n; }

    int k = 0;
    for (int i = 0; i < n && k < max; ++i) {
        const String s = WiFi.SSID(i);
        strncpy(out[k].ssid, s.c_str(), sizeof(out[k].ssid) - 1);
        out[k].ssid[sizeof(out[k].ssid) - 1] = 0;
        if (!out[k].ssid[0]) strcpy(out[k].ssid, "<hidden>");
        out[k].rssi    = WiFi.RSSI(i);
        out[k].channel = WiFi.channel(i);
        // Case-insensitive on purpose: RY-OPS vs ry-ops was a real bug here
        // (ADR 0007), and the scan screen exists to make that visible.
        out[k].match   = ieq(out[k].ssid, wanted);
        out[k].auth    = (uint8_t)WiFi.encryptionType(i);
        ++k;
    }
    WiFi.scanDelete();

    for (int i = 1; i < k; ++i) {          // insertion sort, strongest first
        ScanEntry t = out[i]; int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) { out[j + 1] = out[j]; --j; }
        out[j + 1] = t;
    }

    // Also dump to serial. A photo of a 40-column screen is a poor evidence
    // channel; this makes a capture answer the question directly, including
    // the channel distribution that shows whether dwell was adequate.
    Serial.printf("--- scan: %d APs, looking for '%s' ---\n", k, wanted ? wanted : "");
    for (int i = 0; i < k; ++i)
        Serial.printf("[%2d] ch%-3d %4ddBm %-6s \"%s\"%s\n", i, (int)out[i].channel,
                      (int)out[i].rssi, authName(out[i].auth), out[i].ssid,
                      out[i].match ? "   <== MATCH" : "");
    Serial.println("--- end scan ---");
    return k;
}

}  // namespace wifimgr
