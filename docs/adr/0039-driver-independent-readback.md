# ADR 0039 — A driver-independent GRAM readback frame source

**Status:** Accepted — implemented (`SpiReadbackFrameSource`), **unverified on
real hardware**. Builds clean against ESP-IDF's `spi_master` driver; the
actual read correctness (dummy-bit count, RGB888->565 truncation, CS timing)
needs `checkPattern()` run on a real device before it's trusted for anything
beyond compiling.
**Deciders:** firmware owner
**Related:** ADR 0002 (proved 3-wire SIO GRAM readback works on this panel —
this ADR does not re-litigate that), ADR 0038 (`IHostAdapter`, the seam this
fills). First consumer: `launcher-adv-mirror` ADR 0002, which discovered the
gap this ADR closes.

## Context

`launcher-adv-mirror` ADR 0002 assumed `ReadbackFrameSource` could be reused
unmodified as a Launcher adapter's frame source. Checked directly against
Launcher's build: it can't. `ReadbackFrameSource::fetchTile()` calls
`M5.Display.readRect(...)` — the M5Unified/M5GFX API — and Launcher's
`m5stack-cardputer` env never links M5Unified/M5GFX at all (that code path in
Launcher's `src/tft.h` is gated behind `USE_M5GFX`, used only by its E-Paper
boards). Launcher draws through `Arduino_GFX`'s `Arduino_ST7789`, which has no
read capability anywhere in its class hierarchy — confirmed by inspecting
`Arduino_DataBus.h`: every virtual method is write-only (`writeCommand`,
`writePixels`, `writeBytes`, ...), no `readPixels`/`beginRead`/`endRead`.

**This is not a re-run of ADR 0002's actual risk.** Whether 3-wire SIO GRAM
readback works on this panel at all is already answered — shipped, and
verified at boot by `ReadbackFrameSource::selfTest()` scoring in the field.
The gap here is narrower: the *specific library* that issues those SPI
transactions (M5GFX) isn't present in a Launcher build. What's needed is the
same already-proven command sequence, expressed without M5GFX.

**That sequence, read directly from M5GFX's own source**
(`Panel_LCD.cpp:412`, `Bus_SPI.cpp:1004-1063` in the vendored copy under
`.pio/libdeps/cardputer-adv/M5GFX/`):

```
setWindow(x, y, x+w-1, y+h-1)      // CASET/RASET — sets the read window
write_command(RAMRD)               // 0x2E, DC low then high per usual cmd/data framing
beginRead(dummy_read_pixel)         // switch bus to read mode, clock out N dummy bits
readPixels(dst, ..., len)           // clock in len pixels at rgb888_3Byte, convert to RGB565
endRead()                           // restore write-mode clock/bus settings
```

The one wrinkle: M5GFX's `Bus_SPI` implements `beginRead`/`endRead`/`readData`
by writing ESP32 SPI peripheral registers directly (`SPI_USER_REG`,
`SPI_CLOCK_REG`, `SPI_CMD_REG` via raw pointers) — a LovyanGFX performance
choice, not something intrinsic to the read protocol. Porting *that* would
mean assuming exclusive, register-level ownership of the SPI3 peripheral,
which is a real problem if anything else on the bus goes through a different
driver layer.

**Checked whether that's actually a problem for Launcher: it isn't.** Launcher's
bus object is `Arduino_HWSPI` (`display.cpp:84`,
`new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, &SPI)`), and
`Arduino_HWSPI` (`lib_modules/Arduino_GFX/src/databus/Arduino_HWSPI.cpp`) is a
thin wrapper around Arduino's own `SPIClass`, which on arduino-esp32 is itself
built on the standard ESP-IDF `spi_master` driver — not raw register access.
So Launcher's own SPI usage already goes through the standard, safe,
multi-device-per-bus path ESP-IDF provides. There is no raw-register
collision to design around here, because nothing on this bus is doing that.

## Decision

Add a new `IFrameSource` implementation to this library —
`SpiReadbackFrameSource` — that performs the same read sequence via the
standard ESP-IDF `spi_master` driver instead of M5GFX. As implemented, one
detail resolved during coding that's worth recording here rather than only
in the diff: device-level `dummy_bits` stays 0 (a nonzero value would
insert a dummy phase into *every* transaction through the device handle,
including the plain CASET/RASET/RAMRD command writes, corrupting their
framing); the 16-bit dummy phase is applied per-transaction only on the
final read, via `spi_transaction_ext_t` + `SPI_TRANS_VARIABLE_DUMMY`.

- Takes pin config (MOSI/SCLK/DC/CS/RST), read clock speed, and dummy-bit
  count through its constructor/a `Config` struct — no dependency on a global
  `M5.Display` or any particular write-side driver.
- Calls `spi_bus_add_device()` to attach its own, independent
  `spi_device_handle_t` to whichever `SPI3_HOST` bus the host's own driver
  already brought up with `spi_bus_initialize()` — the standard, supported way
  multiple logical devices share one ESP-IDF-managed SPI bus. It does not
  initialize the bus itself; the host's own display driver already did that.
- Issues one `spi_device_transmit()` per tile with `SPI_DEVICE_HALFDUPLEX`,
  the `RAMRD` command in the command phase, `dummy_read_pixel` dummy bits, and
  an `rx_buffer` sized for the tile at 3 B/px — the same window-then-read
  shape as `Panel_LCD::readRect()`, expressed through the driver API instead
  of direct registers.
- Converts the RGB888-on-the-wire read data to RGB565 the same way
  `ReadbackFrameSource::fetchTile()` already does, including the byte-order
  care that ADR 0002 (and ADR 0006, the byte-order bug it caught) worked out.
- Ships its own `selfTest()`, same pattern as `ReadbackFrameSource`: draw a
  known pattern, read it back, report percent match. The sequence is proven;
  this specific implementation of it is not, until it's run on real hardware.

`ReadbackFrameSource` (M5GFX-based) is untouched *behaviorally*. Its method
bodies did move — out of `CardputerMirror.cpp` into their own
`ReadbackFrameSource.cpp`, guarded by `#if __has_include(<M5Unified.h>)` —
because `CardputerMirror.cpp` contains `Mirror` itself, and `Mirror` only
ever touches a frame source through `IFrameSource`. Leaving `M5Unified.h`
included there unconditionally would have broken compilation the moment
`Mirror` got built into a host without it — which is exactly the Launcher
case this whole ADR exists for. A host without M5Unified simply compiles
`ReadbackFrameSource.cpp` to nothing and never constructs the class; nothing
links a missing symbol, because nothing references one. This is a second,
additive `IFrameSource`, not a rewrite of the first — zero regression risk
to the existing standalone example, verified by an unchanged
`env:cardputer-adv` build (RAM/Flash identical).

**One real implementation risk worth naming now, not discovering later:**
ESP-IDF's half-duplex + DMA combination has documented restrictions for
transactions that mix a command/dummy phase with a large read. Tile reads
here are 8,100 B (`kTilePx * 3`), large enough that DMA matters for
throughput. This needs to be verified against the actual driver behavior
during implementation, not assumed to just work because the API accepts the
configuration.

## Consequences

**Positive**

- Removes the library's accidental M5GFX coupling entirely — any future
  adapter for a non-M5GFX host (Bruce, nemo, ...) gets a working frame source
  for free, which is the actual point of ADR 0001's architecture.
- Uses ESP-IDF's standard, documented bus-sharing mechanism, not a
  hand-rolled one — no assumption about what else is on the bus beyond "it
  was initialized by someone" is required.
- `ReadbackFrameSource` keeps working exactly as it does today; nothing about
  the standalone example changes.

**Negative**

- Real new code on a path where correctness is genuinely hard to verify
  without hardware — a wrong dummy-bit count or half-duplex/DMA
  misconfiguration fails silently as garbage pixels or a hang, not a compile
  error. `selfTest()` exists specifically because of this, same as ADR 0002.
- One more `IFrameSource` implementation to maintain going forward, alongside
  `ReadbackFrameSource`.

**Neutral**

- If a future host *does* use M5GFX, it should still prefer
  `ReadbackFrameSource` — this ADR doesn't deprecate it, it fills the gap for
  hosts that don't have it.

## Alternatives considered

- **Port M5GFX's raw register-level `Bus_SPI` implementation directly.**
  Rejected: assumes exclusive register-level ownership of the SPI
  peripheral, which is unnecessary complexity here — Launcher's own bus usage
  already goes through the standard driver, so there's nothing to gain by
  bypassing it too, and real risk in doing so incorrectly.
- **Patch `Arduino_HWSPI` (Launcher's vendored `Arduino_GFX` submodule) to add
  a read method.** Rejected: edits a dependency Launcher itself tracks
  upstream, which is worse than adding independent code that touches neither
  Launcher nor `Arduino_GFX` at all.
- **Write this as Launcher-specific code inside the adapter, not the core
  library.** Rejected: the gap is "hosts without M5GFX," not "Launcher
  specifically" — writing it in the core means every future adapter with the
  same gap gets it for free instead of re-solving it.
