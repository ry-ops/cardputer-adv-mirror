# ADR 0010 — On-device menu

**Status:** accepted
**Context:** third consecutive WiFi failure diagnosed only by flashing new firmware

## Problem

Every capability this project has built lives somewhere the device cannot
reach on its own:

| Capability | Where you use it | Available when WiFi is down? |
|---|---|---|
| Mirror | browser over WiFi | no |
| SD formatter | browser over WiFi | no |
| WiFi diagnostics | serial monitor over USB | only with a host attached |

The one situation where you most need diagnostics — WiFi has not come up — is
precisely the situation where two of the three are unreachable.

That produced the loop this project kept running: edit, build, flash, replug,
squint at boot output, guess again. An iPhone hotspot that only wakes when you
open its Settings pane cannot be debugged that way at all: by the time the
device finishes rebooting, the phone is asleep again.

## Decision

Put the state on the device's own screen, and make the two diagnostic verbs —
**scan** and **retry** — things it can do while running.

Screens: **Home** (status summary + nav), **Network**, **Scan**, **Storage**,
**System**, **Mirror**.

The load-bearing one is Network. It shows `STATE`, `SSID`, `IP`, and — when the
join failed — `WHY` and how many networks the scan saw. Those strings already
existed; they were only ever printed to a serial port nobody could read at the
time. `[ Retry connect ]` re-runs the entire bring-up with no reboot, which
means the hotspot can be woken *and then* joined, in that order, without a
build.

Scan lists every 2.4 GHz network strongest-first and marks the configured SSID
with `*` on a **case-insensitive** match. That is ADR 0007's bug, made visible
on the device.

## Runtime re-entry

`wifimgr::begin()` took its profile list as arguments, so nothing could call it
again later. `wifi_manager_rt` stores those pointers at boot (`remember()`) and
exposes `retry()` and `scanOnce()`. The profiles are string literals with static
lifetime, so holding pointers is safe; anything dynamic would need copying.

## Repaint policy — the non-obvious constraint

The menu draws to the same panel the mirror reads back. That part is free: the
mirror only ever *reads* GRAM, so it mirrors the menu with no coordination.

But the mirror transmits **only tiles that changed**. A menu repainting on a
1 s timer would mark all 12 tiles dirty every second and stream a byte-identical
screen forever, defeating the dirty-tile scheduler entirely.

So repaint is **change-driven**: `liveHash()` fingerprints exactly the values
the current screen displays (battery, client count, heap, uptime, format
progress, selection) and is polled at 4 Hz. Repaint happens when the hash moves.
A static screen produces zero redraws and zero mirror traffic.

The System screen intentionally mixes uptime into the hash, so it *does* tick
once a second — that one screen is a live clock by design.

## Deliberate omission: no format on the device

Storage offers `[ Remount ]` but not format. A destructive, irreversible action
needs a confirmation step that a 4-row keyboard cannot make hard to trigger by
accident. The browser has a two-step confirm (ADR 0008); that stays the only
path. The screen says so rather than leaving the absence unexplained.

## Input mapping

The ADV reports arrow keys only as `fn` + `;` `.` `,` `/`, which is awkward
one-handed. Accepted as up/down: `;`/`.` and `w`/`s`. Back is `` ` `` or
backspace, select is Enter. `handleKey()` returns whether it consumed the
event, so a future full-screen app can still receive typing.

## Consequences

- `+30 KB` flash (1,015,809 -> 1,045,921), `+1.5 KB` RAM.
- The boot status text is gone; those values are now on screens that stay
  current instead of a snapshot that goes stale immediately.
- WiFi can be re-attempted without a flash — the loop this ADR exists to end.
