/*
 * wifi_manager_rt — runtime re-entry into the WiFi bring-up.
 *
 * WHY: every WiFi diagnosis this project has done cost a full
 * edit-build-flash-replug cycle, and the flash itself takes the port away, so
 * the failure and the observation are minutes apart. An iPhone hotspot that
 * only wakes when you open its Settings pane cannot be debugged that way at
 * all -- by the time the device reboots, the phone is asleep again.
 *
 * Exposing "scan again" and "join again" as things the device can do while
 * running collapses that loop to a keypress.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "wifi_manager.h"

namespace wifimgr {

// Remembered from the last begin() so retry() needs no arguments.
void  remember(const Profile* profiles, size_t count,
               const char* apSsid, const char* apPass, const char* hostname);

// Re-run the whole bring-up now. Safe to call repeatedly.
Result retry();

// The most recent Result, so any screen can render current state.
const Result& last();
void setLast(const Result& r);

// The SSID the device is CONFIGURED to want -- profiles[0].
//
// Not the same as last().ssid: in SoftAP fallback that field holds the
// FALLBACK AP's own name, so anything matching against it is asking the wrong
// question entirely.
const char* targetSsid();

// One scan pass, for display. Returns count, or <0 on failure.
// `out` receives up to `max` entries sorted strongest-first.
struct ScanEntry {
    char    ssid[33];
    int32_t rssi;
    int32_t channel;
    bool    match;
    uint8_t auth;      // wifi_auth_mode_t: 0=OPEN 2=WPA 3=WPA2 5=ENTERPRISE 7=WPA3
};

// Short label for a wifi_auth_mode_t. "ENT" means 802.1X, which a plain
// SSID+passphrase join CANNOT satisfy -- worth seeing before blaming the radio.
const char* authName(uint8_t auth);
int scanOnce(ScanEntry* out, int max, const char* wanted);

}  // namespace wifimgr
