# ADR 0037 — Why control appeared to need USB: modem sleep, and a counter that lied

**Status:** Accepted — implemented

## Context
Control from the dashboard worked with the ADV plugged into USB and appeared
dead on battery. The display mirror kept working in both cases, which is what
made this look like a control-path problem rather than a link problem.

## The framing was wrong
USB is not in the control path at all. There is no serial transport for keys:
the browser opens ONE WebSocket to `/ws`, tiles come back on it and key events
go out on it. Nothing in the firmware reads USB except `banner()` on the letter
'b'. If USB genuinely gated control, the mirror would have to be dead too.

So the question is not "what does USB provide" but "what else is different when
the cable is in". Two things were, and both are real.

## Cause 1 — WiFi modem sleep, and why it hits control but not display
We never called `WiFi.setSleep()`. The Arduino core's default on ESP32-S3 is
`WIFI_PS_MIN_MODEM` (`WiFiGeneric.cpp:769`, guarded `#if CONFIG_IDF_TARGET_ESP32S2`
— the S3 takes the else branch), applied automatically on the `STA_START` event
at `WiFiGeneric.cpp:1046`. We had modem sleep on and never chose it.

Modem sleep is **asymmetric**, which is exactly why the symptom looked the way
it did:

- **Outbound** (our tile stream): transmitting wakes the radio. Frames leave
  immediately. The display is unaffected.
- **Inbound** (every keypress): the receiver is parked between beacons. The AP
  must buffer the frame and advertise it at the next DTIM; we wake, collect it,
  and only then does the key exist on the device. That is up to a beacon
  interval of added latency per packet — ~100 ms typical, worse on a busy or
  distant AP.

On a TCP socket already carrying a heavy opposing stream, that inbound delay
compounds: delayed ACKs, a receive window that does not open promptly, and
retransmits that arrive after the user has given up and called it broken.

**USB correlates with the fix without being the cause.** A device on the bench
is a metre from the AP with a strong, clean link; a device on battery is across
the room. Same firmware, different margin — and modem sleep eats margin on the
inbound direction only.

`WiFi.setSleep(false)` in `wifimgr::begin()`. It costs idle current, which this
device does not have anyway: it streams the framebuffer continuously whenever a
browser is attached.

## Cause 2 — the browser counted keys it never sent
`send()` was:

    const send = o => ws && ws.readyState === 1 && ws.send(JSON.stringify(o));

It yields `false` and transmits nothing when the socket is closed or still
connecting. Every call site then did:

    send({t:'key', ...});
    noteSent(k.r, k.c);          // unconditional

so "keys sent" incremented and the cap lit `hit` for presses that went nowhere.
The comment directly above `noteSent()` states that these counters exist to
separate "browser never sent it" from "device never got it". They could not do
that, because the browser was counting its own drops as sends.

This matters most in precisely the situation being diagnosed: `onclose` retries
after 1200 ms, and **every press during that window is silently discarded while
the UI reports success**. A weak link causes socket closes; socket closes cause
invisible drops; the invisible drops read as "control is broken".

`send()` now returns a boolean and all three call sites pass it to
`noteSent(r, c, ok)`, which counts a drop instead and turns the new
`keys dropped` counter red.

## Consequences
- The heartbeat now prints `keys=applied/dropped rssi=N`. The browser's count
  is the browser's opinion; this is the device's. If the browser counts a press
  and this does not move, the packet never arrived — a link problem. If both
  move and nothing happens on screen, it is a menu problem. `keyinject` had
  tracked both numbers all along and never exposed them.
- Assertions are on SOURCE, not behaviour: jsdom has no WebSocket peer, so
  exercising the closed-socket path would test the mock rather than the page.
  They assert `send()` returns a boolean, that all three call sites consume it,
  and that no site sends-then-counts as two statements.
- If control is still lossy after this, `rssi` in the heartbeat is the next
  thing to read. Below about -75 dBm the fix is an AP or an antenna, not code.
- Not adopted: an application-level ACK per key. The device counter plus the
  browser counter already localise the fault to a side, and a per-key round
  trip would add traffic to the link whose margin is the problem.
