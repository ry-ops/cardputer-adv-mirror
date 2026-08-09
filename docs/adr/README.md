# ADR Index — Cardputer ADV Browser Mirror

Architecture Decision Records for mirroring / remote-controlling an
**M5Stack Cardputer ADV** (`board_M5CardputerADV`, enum 24) in a web browser.

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-panel-tee-mirror.md) | Panel tee mirror (`Panel_Mirror` wraps `Panel_ST7789`) | Proposed |
| [0002](0002-gram-readback-mirror.md) | GRAM readback mirror (non-invasive) | **Accepted — implemented** |
| [0003](0003-browser-simulator.md) | Pure browser simulator (no device) | Proposed |
| [0004](0004-full-remote-control.md) | Mirror + full remote control | Proposed |

## Hardware facts these ADRs rest on

All verified by reading M5GFX / M5Unified / M5Cardputer sources, not datasheets.

| Property | Value | Source |
|---|---|---|
| Board enum | `board_M5CardputerADV = 24` | `M5GFX/src/lgfx/boards.hpp:35` |
| Panel | `Panel_ST7789`, 135x240 native | `M5GFX.cpp` autodetect |
| Logical screen | **240x135** (`rotation = 1`), `offset_x=52`, `offset_y=40` | `M5GFX.cpp` |
| SPI | `SPI3_HOST`, write 40 MHz, **read 16 MHz** | `M5GFX.cpp` |
| Pins | MOSI 35, SCLK 36, DC 34, CS 37, RST 33, BL 38 (PWM ch7) | `M5GFX.cpp` |
| **MISO** | **not wired (`-1`), `spi_3wire = true`** | `M5GFX.cpp` |
| Readback | `cfg.readable = true`; `_read_depth = rgb888_3Byte` | `Panel_LCD.hpp:140` |
| Keyboard | **TCA8418 I2C controller**, `matrix(7,8)`, INT **GPIO11** | `TCA8418.cpp` |
| Keyboard seam | `Keyboard_Class::begin(std::unique_ptr<KeyboardReader>)` | `Keyboard.h:153` |

Derived budget (see `tools/verify_codec` for the arithmetic):

- Shadow framebuffer, RGB565: 240x135x2 = **64,800 B (63.3 KiB)**
- Full-frame readback at 3 B/px: **97,200 B -> 48.6 ms @ 16 MHz -> ~20.6 fps ceiling**
- Tile 60x45: 8,100 B read -> **4.05 ms/tile**, 12 tiles/screen

## Does starting with #1 set the base for #4?

**Yes — and so does starting with #2.** This was the deciding factor in the
sequencing, so it is worth stating precisely.

The system splits into five layers. Only the *frame source* differs between
options:

```
          +-----------------------------+
 layer 5  |  browser UI (canvas, keys)  |  shared by 1, 2, 4
 layer 4  |  wire protocol + RLE codec  |  shared by 1, 2, 4
 layer 3  |  dirty-tile scheduler       |  shared by 1, 2, 4
 layer 2  |  IFrameSource  <---- THE ONLY DIFFERENCE
          |    ReadbackFrameSource (#2) |
          |    TeeFrameSource      (#1) |
 layer 1  |  input injection            |  #4 only
          +-----------------------------+
```

- **#1 -> #4**: #4 *is* #1 plus layer 1 (input injection) and a control
  channel. #1 builds layers 2-5; #4 adds layer 1. Nothing in #1 is thrown away.
- **#2 -> #1**: swapping `ReadbackFrameSource` for `TeeFrameSource` is a
  one-line change at the call site. Layers 3-5 are untouched.
- **#2 -> #4**: #2 builds layers 3-5, which is the majority of the work by
  volume. #4 then needs layer 2 (tee) + layer 1 (input).

So the chosen order **#2 -> #1 -> #4** is strictly incremental: no layer is
built twice, and #2 pays for the protocol/UI work that #1 and #4 both inherit.
The reason to start at #2 anyway is risk: #2 answers the one question no amount
of source reading can settle — *does ST7789 GRAM readback actually work over
3-wire SIO on this panel?* If it does not, #2 is dead and #1 becomes the only
mirroring path; better to learn that from a 2-line integration than after
building a full tee.

That question is why the firmware ships a **boot self-test** (`selfTest()`):
it draws a known pattern, reads it back, and reports percent match before you
trust a single frame.
