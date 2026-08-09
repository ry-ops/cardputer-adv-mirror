/*
 * wifi_manager — multi-profile WiFi bring-up with actionable diagnostics.
 *
 * Rationale (ADR 0007): the first flash silently fell back to SoftAP because
 * WIFI_SSID was nullptr, and the only visible symptom was the URL reading
 * 192.168.4.1 — which is indistinguishable at a glance from a successful join.
 * This module makes the outcome explicit and, when it fails, says why.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>

namespace wifimgr {

struct Profile {
    const char* ssid;
    const char* password;
};

enum class Outcome : uint8_t {
    Connected,      // joined a profile in STA mode
    SoftAP,         // no profile joined; SoftAP is up
    Failed          // nothing came up at all
};

struct Result {
    Outcome     outcome     = Outcome::Failed;
    const char* ssid        = nullptr;   // network actually joined (or AP ssid)
    int32_t     rssi        = 0;
    int32_t     channel     = 0;
    uint32_t    elapsedMs   = 0;
    int         scanCount   = -1;        // networks seen; -1 if scan failed
    bool        seenInScan  = false;     // was the top profile even visible?
    const char* detail      = "";        // human-readable failure cause
    const char* hostname    = nullptr;   // hostname requested from DHCP
    bool        openNet     = false;     // joined an open (no-passphrase) SSID
    bool        mdnsUp      = false;     // responder started -> <hostname>.local

    bool ok() const { return outcome == Outcome::Connected; }
    IPAddress ip() const {
        return outcome == Outcome::Connected ? WiFi.localIP() : WiFi.softAPIP();
    }
};

/*
 * Scan, then try each profile in order, then fall back to SoftAP.
 *
 * A profile absent from the scan is skipped without burning the connect
 * timeout — that is the common case for a 5 GHz-only or sleeping hotspot, and
 * `detail` will say so explicitly rather than reporting a generic timeout.
 *
 * `hostname` is applied before the join (the DHCP client sends it in the
 * request, so setting it afterwards has no effect) and is also the mDNS
 * name: pass "cardputer" and the device answers to cardputer.local. That
 * matters here because the hotspot hands out DHCP leases that change across
 * reboots, so the numeric address is not a stable way to reach the mirror.
 */
Result begin(const Profile* profiles, size_t count,
             const char* apSsid, const char* apPass,
             const char* hostname = "cardputer",
             uint32_t perProfileMs = 12000);

// Decode wl_status_t into something printable.
const char* statusName(wl_status_t s);

}  // namespace wifimgr
