# ADR 0014 — The scan screen was matching against the wrong SSID

**Status:** accepted
**Context:** user reports RY-OPS still absent after the clean-scan fix (ADR 0011)

## The defect

`runScan()` in the menu did this:

    const auto& w = wifimgr::last();
    s_scanN = wifimgr::scanOnce(s_scan, 24, w.ssid);

`last()` is the most recent bring-up **result**. Read what `r.ssid` holds in
each outcome (`wifi_manager.cpp`):

    line 169:  r.ssid = prof.ssid;              // Connected  -> the joined network
    line 199:  r.ssid = up ? apSsid : nullptr;  // SoftAP     -> OUR OWN AP's name

The device is in SoftAP fallback. So `w.ssid` was `"CardputerADV"` -- the name
of the access point the Cardputer itself is broadcasting.

Every scan result was therefore compared against the wrong string. The green
`*` match marker **could not light up for RY-OPS under any circumstances**,
even with the network sitting in the list at full strength. And in the
`Outcome::Failed` case `r.ssid` is `nullptr`, which is worse.

ADR 0011 fixed the scan itself. This defect meant the fix could not be seen.

## Why this went unnoticed for a whole cycle

The instruction I gave was "if RY-OPS appears with a green `*`". Both halves
were unreliable: the marker was broken by this defect, and "appears" asked a
human to visually scan a 24-row list on a 40-column, 135-pixel-tall screen.

A negative report from that setup is ambiguous between:

1. the AP is genuinely not being received
2. it is in the list and was missed while scrolling
3. it is in the list and the marker failed to render (this defect)

Those need completely different responses, and the report could not
distinguish them. **That is a design failure in the diagnostic, not a user
error.**

## Fixes

1. `wifimgr::targetSsid()` -- returns `profiles[0].ssid`, the network we are
   CONFIGURED to want. Independent of outcome. `runScan()` uses it.
2. **Verdict line** at the top of the scan screen, stated outright:
   `RY-OPS FOUND ch6 -24dBm` or `RY-OPS NOT RECEIVED (23 APs seen)`.
   No visual searching, and no ambiguity in the resulting report.
3. **Serial dump** of every scan, with channels, RSSI, and an explicit
   `<== MATCH`. A photo of a 40-column screen is a poor evidence channel; a
   capture answers it directly and shows the channel distribution, which is
   what reveals whether per-channel dwell was adequate.

## The pattern, again

ADR 0013 named it: asking a proxy question instead of the real one.

| Wanted to know | Asked instead | Failed when |
|---|---|---|
| what network do we want? | what network did the last attempt end on? | attempt ended in fallback |

Fourth instance in this project. The others: byte-exact SSID (0007), mode
equality during AP_STA (0011), mount-on-repaint (0013).

## Consequences

- `+436 B` flash.
- The scan screen answers the question instead of presenting evidence for the
  user to interpret.
- A negative result is now trustworthy: `NOT RECEIVED (n APs seen)` with n
  large means the radio is working and the target is genuinely absent, which
  finally isolates the band hypothesis.
