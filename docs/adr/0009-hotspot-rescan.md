# ADR 0009 — Hotspot rescan, and never show only the mDNS name

**Status:** Accepted — implemented
**Context:** device displayed `http://cardputer.local` but was unreachable

## Symptom

The screen read `http://cardputer.local`. The URL did not resolve.

## What was actually wrong

Serial said it in one line:

    ip=192.168.4.1   clients=0

`192.168.4.1` is the **SoftAP fallback** address. The device had not joined the
hotspot at all. mDNS was working perfectly — it was answering for
`cardputer.local` on the device's own access point, which nothing was joined to.

Two independent defects, and they concealed each other.

## Defect 1 — a single boot scan misses a dormant iPhone hotspot

An iPhone Personal Hotspot powers its radio down when no client is attached. It
wakes when the owner opens the Settings > Personal Hotspot pane, or when a
known client associates. The ADV boots in a few seconds, scans exactly once,
does not see the hotspot, and falls back — all before the phone has any reason
to wake up.

This is *not* the case-sensitivity bug from ADR 0007, and the fix from that ADR
is still correct and still needed. The scan-list matching is fine; there was
simply nothing in the scan list to match.

**Fix:** rescan up to `kScanPasses = 4` times with `kRescanDelayMs = 3500`
between passes — roughly a 15 s window before falling back. The full 20-line
scan dump prints on the first pass only; later passes print one line
(`rescan 2: 23 networks, "ry-ops" FOUND`) so the useful diagnostic is not
buried.

## Defect 2 — the display told a confident lie

The banner was changed in ADR 0008 to prefer the mDNS name:

    if (gWifi.mdnsUp) print("http://%s.local", hostname);
    else              print("http://%s", ip);

Because mDNS starts in SoftAP mode too (deliberately — so the `.local` name
works in both outcomes), `mdnsUp` was true during the fallback. The screen
therefore looked **identical** in the working case and the failed case.

The numeric IP was the one piece of information that distinguished them, and
the change had removed it from the screen.

**Fix:** always print the IP; print the `.local` name as an additional line
when mDNS is up. `192.168.4.1` on screen now means "fallback" at a glance.

The general lesson: a friendly alias is a fine *addition* to a status display
and a bad *replacement* for one. The alias resolved correctly in both states,
so it carried no diagnostic information at all.

## Known limitation — `.local` may still not resolve on an iPhone hotspot

Even once the device joins, `cardputer.local` is not guaranteed to work from a
laptop on the same Personal Hotspot. mDNS is multicast, and client-to-client
multicast is not reliably forwarded between hotspot clients. This is a property
of the hotspot, not a bug in the firmware.

`http://<ip>` always works. That is the second reason the IP belongs on screen
permanently, and it is why the SSID/IP line is the primary display and the
`.local` line is secondary.

## Consequences

- `+436 B` flash.
- Worst-case boot is ~15 s longer *only when the hotspot is absent*. A hotspot
  present on the first scan connects exactly as fast as before.
- The screen can no longer show a reachable-looking URL while in fallback.
