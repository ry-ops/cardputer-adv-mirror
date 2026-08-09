# ADR 0002 — GRAM readback mirror (non-invasive)

**Status:** **Accepted — implemented** (`lib/CardputerMirror`)
**Deciders:** firmware owner
**Related:** 0001 (swappable frame source), 0004 (adds input)

## Context

We want a browser mirror that works against *existing* firmware with the
smallest possible diff, and we want to learn early whether display readback is
viable on this hardware at all.

M5GFX configures the ADV panel with `cfg.readable = true`. Critically, the
Cardputer family wires **no MISO line**:

```cpp
bus_cfg.pin_mosi  = GPIO_NUM_35;
bus_cfg.pin_miso  = (gpio_num_t)-1;   // not wired
bus_cfg.spi_3wire = true;             // -> SPI_SIO, half-duplex on MOSI
```

Reads therefore run half-duplex over MOSI. This is not speculative: M5GFX's own
autodetect identifies the panel via `_read_panel_id(bus_spi, GPIO_NUM_37)` and
matches `(id & 0xFB) == 0x81`, so register reads over SIO demonstrably work.
Whether *GRAM* reads (`RAMRD`, 0x2E) work as reliably is the open risk.

Read depth is fixed at 3 bytes/pixel regardless of write depth
(`Panel_LCD.hpp:140`: `_read_depth = rgb888_3Byte`), which sets the budget:

| Quantity | Value |
|---|---|
| Full frame read | 97,200 B -> **48.6 ms @ 16 MHz** -> ~20.6 fps ceiling |
| Tile 60x45 | 8,100 B -> **4.05 ms** |
| Shadow RGB565 | 64,800 B (63.3 KiB) |

## Decision

Poll the panel's GRAM with `M5.Display.readRect()` from a **time-budgeted,
incremental tile scanner**, diff each tile against a shadow buffer by CRC, and
push only changed tiles to the browser over WebSocket.

Integration is two lines:

```cpp
CardputerMirror.begin();    // in setup()  — starts WiFi + HTTP/WS
CardputerMirror.update();   // in loop()   — scans within a time budget
```

### The scanner runs on the main task, not a background task

This is the load-bearing decision of this ADR. **LGFX/M5GFX is not
thread-safe.** A background task calling `readRect()` while the application
task is mid-draw interleaves two SPI transactions on the same bus and produces
corrupted pixels or a bus fault.

Rather than impose a mutex on the application (which would require touching
every draw site — destroying the "non-invasive" property), `update()` is called
from `loop()` and inherits the application's own serialization for free. It
reads at most `budgetUs` (default 4,500 us -> ~1 tile) per call and returns,
so it never blocks the application for long.

WiFi and WebSocket work stay on the AsyncTCP task; they touch no SPI.

### Boot self-test

`selfTest()` draws a known pattern, reads it back, and reports percent match.
It runs at boot and its result is sent in the `hello` message, so the browser
shows whether readback is trustworthy before any frame is believed.

## Consequences

**Positive**

- Two-line integration. Agnostic to *how* the application draws — direct LGFX,
  canvas/sprite blits, LVGL, or a third-party UI toolkit all mirror identically.
- Cannot corrupt the application's draw path; it only ever reads.
- Builds layers 3-5 (scheduler, protocol, browser UI) that ADRs 0001 and 0004
  reuse verbatim.
- Answers the readback-viability question cheaply and early.

**Negative**

- **Frame rate is capped at ~20 fps** by the 16 MHz read clock, and lower in
  practice because reads contend with the application's own 40 MHz writes.
- Steals SPI bus time from the application, so heavy-drawing firmware will
  visibly slow down. `budgetUs` is the throttle.
- Tearing: a tile may be read while being drawn, so a frame can mix old and new
  content. Acceptable for a mirror, not for pixel-exact regression testing.
- Change detection is CRC-based sampling, so a change that is drawn *and*
  reverted between two scans of the same tile is missed entirely.
- **Risk: some ST7789 panels return unreliable or byte-shifted GRAM data.**
  Mitigated, not eliminated, by `selfTest()` plus runtime R/B-swap and invert
  toggles in the browser UI.

**Neutral**

- +63.3 KiB SRAM shadow +5.4 KiB tile buffer. Allocated from PSRAM when
  present, internal SRAM otherwise.

## Alternatives considered

- **Background task + global SPI mutex.** Rejected: the mutex must be taken by
  the *application's* draw calls to be correct, which means editing every draw
  site and forfeiting non-invasiveness.
- **Read the whole frame in one `readRect`.** Rejected: a single 48.6 ms
  blocking call inside `loop()` would stall input handling and audio.
- **Scan on a timer ISR.** Rejected: SPI transactions are illegal from ISR
  context here, and the same thread-safety problem applies.
