# ADR 0007 — Multi-profile WiFi manager with explicit failure reporting

**Status:** Accepted · 2026-08-08

## Context

The first flash of the mirror appeared to work: the device screen read
`Mirror: http://192.168.4.1` and the browser page rendered. It had in fact
never joined any network. `WIFI_SSID`/`WIFI_PASS` in `main.cpp` were both
`nullptr`, so `Mirror::begin()` took its SoftAP branch, and `192.168.4.1` is
the ESP32's *fixed SoftAP address*.

The cost of that ambiguity was the whole session. `clients=0 tiles=0` held for
270 s of heartbeats before anyone noticed no browser had ever connected —
because the failure mode and the success mode look identical at a glance.

Two further hazards were latent:

- Credentials lived in `main.cpp`, i.e. directly in a tracked source file.
- The device is 2.4 GHz only, and the intended network is an iPhone hotspot,
  which iOS defaults to 5 GHz. That failure is invisible: the SSID simply never
  appears, and a naive `WiFi.begin()` reports a generic timeout indistinguishable
  from a wrong password.

## Decision

Add `src/wifi_manager.{h,cpp}`, owning connection policy, plus a gitignored
credentials header.

**1. Credentials out of source.** `include/wifi_credentials.h` (gitignored) holds
a `WIFI_PROFILES` list; `wifi_credentials.example.h` is the tracked template. A
`__has_include` guard emits an actionable `#error` when the real file is absent,
rather than a confusing cascade of undeclared-identifier errors.

**2. Scan before connect.** Profiles are tried in order, but one absent from the
scan is skipped immediately instead of burning the 12 s connect timeout. This
distinguishes *"not visible — 5 GHz-only, asleep, or out of range"* from
*"visible but refused — check passphrase"*. Those need opposite fixes, and the
stock API cannot tell them apart.

**3. The outcome is stated, never inferred.** `Result` carries the outcome, the
SSID actually joined, RSSI, channel, elapsed time, scan count, and a
human-readable `detail`. The device screen colour-codes it: **green** = joined
your network, **yellow** = SoftAP fallback, **red** = no radio. An address alone
is never again treated as evidence of a successful join.

**4. `Config::manageWifi`.** `Mirror::begin()` previously always drove the radio.
With an external manager that is actively harmful — it would tear down a working
STA link and start a SoftAP. The flag defaults to `true` so the library remains
standalone; `main.cpp` sets it `false`.

## Configuration

    ry-ops / <passphrase in gitignored header>   (iPhone personal hotspot)
    fallback AP: CardputerADV / cardputer

## Consequences

- Adding a network is one line in a gitignored header; no logic changes.
- Flash cost 3,888 B (28.0% -> 28.1%), RAM +32 B. Acceptable.
- **iPhone hotspot caveat, baked into the banner text:** iOS defaults the hotspot
  to 5 GHz on recent models, and the ESP32-S3 cannot see 5 GHz at all. If `ry-ops`
  is not in the scan, enable *Settings > Personal Hotspot > Maximize Compatibility*.
  iOS also sleeps the hotspot with no client attached, so it may need waking
  before the device boots.
- A SoftAP fallback is now a loud, colour-coded, reason-carrying event rather
  than a silent default.

## Alternatives rejected

- **`build_flags = -DWIFI_SSID=...`** — puts credentials in `platformio.ini`,
  which is tracked; the exact problem being solved.
- **WiFiManager captive portal** — a large dependency, and it needs a browser on
  the AP to configure, which is the very thing that is broken when WiFi fails.
- **NVS-stored credentials with a serial setup command** — better long-term, but
  requires serial write access, which the agent sandbox does not have.
