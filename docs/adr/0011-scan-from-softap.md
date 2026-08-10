# ADR 0011 — Scanning from SoftAP is not a trustworthy scan

**Status:** Accepted — implemented
**Context:** RY-OPS visible at -24 dBm earlier; now absent from every scan

## The contradiction

Earlier this project captured this, from the device, in AP fallback:

    [ 0] ch6    -24dBm  "RY-OPS"

Channel 6 is 2.4 GHz. -24 dBm is essentially touching the antenna. The ADV's
radio can see this hotspot, and did.

Now the same hotspot is up, with two clients attached, and does not appear in
the device's scan at all. Same room, same hardware, same firmware lineage.

Something other than "the hotspot is invisible" is going on, because the
hotspot demonstrably was not invisible.

## Cause: the scan runs while the SoftAP is beaconing

`WiFi.scanNetworks()` calls `WiFi.enableSTA(true)` internally (verified in the
bundled `WiFiScan.cpp`). Called from AP fallback, that yields **AP_STA**, not
STA: the SoftAP keeps running throughout the scan.

A single radio cannot beacon on the AP's channel and dwell on another channel
at the same time. It time-slices, and per-channel dwell collapses. An AP that
duty-cycles its beacons -- which an idle iPhone Personal Hotspot does -- can
fall entirely into the gaps.

This makes the fallback state **self-perpetuating**: the device drops to
SoftAP, and the SoftAP then degrades exactly the scan needed to escape it.

## Decision

`scanOnce()` now drops the AP for the duration of a manual scan:

    prevMode = WiFi.getMode();
    if (hadAP) { WiFi.mode(WIFI_STA); delay(120); }
    n = WiFi.scanNetworks(false, true, false, /*max_ms_per_chan=*/300);
    if (hadAP) { WiFi.mode(prevMode); delay(80); }

Dwell is also raised from the 120 ms default to 300 ms per channel. A browser
client on the fallback AP is disconnected for ~3 s. That is the right trade:
the user explicitly asked for a scan, and a scan that cannot be trusted is
worth nothing.

## Also: the screen now states what it cannot see

An empty scan result and a 5 GHz-only network look identical on screen, and
that ambiguity has now cost three flashes. The Scan screen carries a permanent
footer:

    2.4GHz only - a 5GHz AP CANNOT appear

and the Network screen adds, when many APs were seen but the configured one was
not:

    HINT   many APs seen, yours absent = 5GHz?

Many networks seen rules out a dead radio, which leaves band as the leading
explanation. Naming that on the device is the point of the menu.

## Remaining hypothesis, if the clean scan still finds nothing

The hotspot has moved to 5 GHz. iOS picks the band, and it will use 5 GHz when
its clients are all 5 GHz-capable -- which is now true here: a Mac and an
iPhone are attached, both 5 GHz-capable, and no 2.4 GHz-only client is present.
Earlier, when the capture showed channel 6, the situation was different.

The ESP32-S3 radio is 2.4 GHz only. This is a hardware limit, not a firmware
one, and no amount of rescanning can defeat it.

The setting that forces 2.4 GHz is **Maximize Compatibility**, in
Settings > Personal Hotspot on the iPhone.

## Consequences

- `+220 B` flash.
- Manual scan briefly drops the fallback AP; documented on screen via the
  "scanning..." state.
- The boot scan is unaffected -- no AP exists yet at that point, so it was
  never subject to this.
