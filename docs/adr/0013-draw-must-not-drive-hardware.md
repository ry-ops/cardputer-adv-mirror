# ADR 0013 — A draw function must not drive hardware

**Status:** Accepted — implemented
**Context:** serial log flooded with SD init errors; heartbeat reported 0.0.0.0
for 250 s while the AP was serving

Two defects, both introduced by me in the menu work, both found in one log.

## Defect 1 — the Storage screen probed the SD card on every repaint

    E (563333) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107
    E (563333) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
    ... repeating several times per second

`drawStorage()` opened with an unconditional `sdmgr::refresh()`. `refresh()`
calls `begin()` when no card is mounted, and `begin()` is a blocking SD probe.

So with no card inserted, **merely looking at the Storage screen** spawned a
mount attempt on every repaint. The errors are the card controller correctly
reporting "nothing here", several times a second, forever.

`0x107` is `ESP_ERR_TIMEOUT` from `send_op_cond`, `0x108` is
`ESP_ERR_NOT_SUPPORTED` from `send_if_cond`. Both are the expected replies to
probing an empty slot. Nothing was broken; the code was asking constantly.

**Rule:** a draw function reads state. It does not drive hardware. Drawing
happens at whatever rate the UI repaints, which is not a rate any peripheral
agreed to.

Fixed: probe on a throttle -- 2 s when a card is mounted (cheap restat), 10 s
when absent (the expensive path). Explicit `[ Remount ]` bypasses the throttle,
because that is a user asking, not a repaint.

## Defect 2 — `ipAddress()` reported 0.0.0.0 while the AP was up

    [   487s] ip=0.0.0.0  clients=0 tiles=0 heap=164400

The old implementation:

    return (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString()
                                       : WiFi.localIP().toString();

`getMode()` returns a bitmask-like enum, and the radio is frequently in
**`WIFI_AP_STA`**, not `WIFI_AP` -- `scanNetworks()` calls `enableSTA(true)`
internally (ADR 0011), and the fallback AP then coexists with an idle STA
interface. The equality test is false in that state, so the function returned
`localIP()`, which is `0.0.0.0` with no association.

The device was serving its AP at 192.168.4.1 the entire time. The status line
did not merely show a wrong address -- it actively concealed a working AP, and
looked identical to a total network failure.

Fixed by testing the thing actually being asked about: association state
(`WiFi.status() == WL_CONNECTED`) with a non-zero address, else the AP address,
else `0.0.0.0`.

## The shared root cause

Both defects come from asking a proxy question instead of the real one:

| Wanted to know | Asked instead | Failed when |
|---|---|---|
| is there a card? | let me try mounting, right now, again | screen was merely visible |
| what address am I reachable at? | is the mode exactly WIFI_AP? | mode was AP_STA |

This is the third time in this project a mode/identity comparison has been too
narrow (ADR 0007: byte-exact SSID; ADR 0011: AP_STA during scan). The pattern
is worth naming: **ESP32 WiFi state is not a small set of clean enums, and
equality tests against it are usually wrong.**

## Consequences

- `+104 B` flash.
- Serial log is legible again; SD errors now appear at most every 10 s and only
  while the Storage screen is open.
- The heartbeat address is trustworthy in both AP and STA states.
