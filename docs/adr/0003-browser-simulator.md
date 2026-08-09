# ADR 0003 — Pure browser simulator (no device)

**Status:** Proposed
**Deciders:** firmware owner
**Related:** 0002 (shares the browser UI shell)

## Context

Mirroring requires hardware in hand, a flash cycle, and a WiFi association for
every UI change. For iterating on *layout and interaction* — menu structure,
font choice, key handling — that loop is slow, and the device adds nothing
because no real peripheral is involved.

The ADV's UI surface is small and fully specified: a 240x135 RGB565 canvas and
a 56-key (4x14) keyboard whose codes we already have from
`Keyboard_def.h` (`KEY_FN 0xff`, `KEY_OPT 0x00`, `KEY_ENTER 0x28`,
`KEY_ESCAPE 0x29`, `KEY_BACKSPACE 0x2a`, `KEY_TAB 0x2b`, arrows `0x4F-0x52`,
`F1-F12` `0x3A-0x45`, `SHIFT` bit `0x80`).

Notably, the ADV's TCA8418 driver **remaps its raw (row, col) into the original
Cardputer's coordinate space** (`TCA8418.cpp::remap`), so a single keymap
serves both devices and the simulator needs only one table.

## Decision

Build a browser-only Cardputer ADV: an HTML canvas at exact 240x135 geometry
with integer nearest-neighbour scaling, plus a rendered 4x14 key grid wired to
the real keymap. Application logic is compiled to WebAssembly against a shim
that implements the subset of the M5GFX drawing API the UI actually uses.

Peripherals (WiFi, IR, RFID, IMU, SD, speaker/mic) are stubbed behind
interfaces that return scripted values.

## Consequences

**Positive**

- Sub-second iteration, no hardware, no flashing. Runs in CI.
- Deterministic and scriptable, so UI states can be snapshot-tested and
  screenshots diffed per commit.
- Shares the canvas/scaling/keyboard chrome with ADR 0002's browser client.
- Lets UI work proceed while hardware is unavailable or in use.

**Negative**

- **It is not the device.** It shows what your code *would* draw, not what the
  panel *is* showing. Timing, RAM pressure, SPI cost, and fragmentation are all
  invisible — precisely the bugs that matter on an 8 MB / no-PSRAM part.
- The M5GFX shim is real, ongoing work; every API the app newly touches must be
  implemented again in JS/WASM.
- Divergence risk: shim and library drift apart silently, so the simulator can
  show a correct screen while the device shows a broken one.
- Stubbed peripherals encode assumptions that may be wrong.

## Alternatives considered

- **QEMU / Wokwi ESP32-S3 emulation.** Runs the real binary, which removes the
  divergence risk. Rejected for now: no TCA8418 or ST7789-over-SIO model exists,
  so the two peripherals that define this device are exactly the two missing.
  Worth revisiting — it dominates this ADR if those models ever land.
- **Native SDL build of the UI layer.** Similar benefits, but not in a browser,
  so it fails the stated requirement.
