# ADR Index — Cardputer ADV Browser Mirror

Architecture Decision Records for mirroring / remote-controlling an
**M5Stack Cardputer ADV** (`board_M5CardputerADV`, enum 24) in a web browser.

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-panel-tee-mirror.md) | Panel tee mirror (`Panel_Mirror` wraps `Panel_ST7789`) | Proposed |
| [0002](0002-gram-readback-mirror.md) | GRAM readback mirror (non-invasive) | **Accepted — implemented** |
| [0003](0003-browser-simulator.md) | Pure browser simulator (no device) | Proposed |
| [0004](0004-full-remote-control.md) | Mirror + full remote control | Proposed — goal achieved via 0002+0017+0037 instead |
| [0005](0005-host-mcp-dev-server.md) | Host-side MCP server for the firmware development loop | Accepted |
| [0006](0006-readback-byte-order.md) | The 25% self-test was a byte-order bug, not unreliable readback | Accepted |
| [0007](0007-wifi-manager.md) | Multi-profile WiFi manager with explicit failure reporting | Accepted |
| [0008](0008-sd-manager.md) | SD card manager: explicit formatting via the FATFS layer | Superseded by 0036 |
| [0009](0009-hotspot-rescan.md) | Hotspot rescan, and never show only the mDNS name | Accepted |
| [0010](0010-on-device-menu.md) | On-device menu | Accepted |
| [0011](0011-scan-from-softap.md) | Scanning from SoftAP is not a trustworthy scan | Accepted |
| [0012](0012-upload-port-guard.md) | Refuse to upload while the serial port is held | Accepted |
| [0013](0013-draw-must-not-drive-hardware.md) | A draw function must not drive hardware | Accepted |
| [0014](0014-scan-matched-wrong-ssid.md) | The scan screen was matching against the wrong SSID | Accepted |
| [0015](0015-show-broadcast-ssid.md) | Show the broadcast SSID, not the configured one | Accepted |
| [0016](0016-open-networks-and-auth-modes.md) | Open networks, auth modes, and the WPA2 association floor | Accepted |
| [0017](0017-remote-keyboard.md) | Remote keyboard: generate the layout, send coordinates | Accepted |
| [0018](0018-key-echo-test.md) | A key that does nothing is not a key that failed | Accepted |
| [0019](0019-adv-has-arrow-keys.md) | The ADV has dedicated arrow keys; I said it did not | Accepted |
| [0020](0020-every-legend-is-a-key.md) | Every legend is a key: dual-legend caps and a solved-for layout | Accepted |
| [0021](0021-keyboard-css-collapse.md) | The keyboard collapsed to one column: a self-referential CSS measure | Accepted |
| [0022](0022-keyboard-is-two-parts.md) | The keyboard is two parts: printed case legend + rubber dome | Accepted |
| [0023](0023-hotspot-ssid-case-and-cred-hygiene.md) | The lowercase hotspot SSID was a real bug, but not the one we hit | Accepted |
| [0024](0024-redacted-credentials-build-guard.md) | A redacted credentials file builds and flashes cleanly, then fails as SoftAP | Accepted |
| [0025](0025-test-the-served-page.md) | Test the page the device serves, not web/index.html | Accepted |
| [0026](0026-remove-keystroke-log.md) | Remove the keystroke log; fix two false claims in the note | Accepted |
| [0027](0027-hamburger-menu.md) | Move occasional controls behind a hamburger; keep modal state visible | Accepted |
| [0028](0028-device-mockup.md) | Device mockup with inset display, zoom pops it out | Accepted |
| [0029](0029-topedge-buttons.md) | BtnG0 and BtnRst move onto the case top edge, unlabelled | Accepted — position/width superseded by 0030 |
| [0030](0030-topface-and-front-detail.md) | Top-face photo corrects the button sides; front-face detail measured | Accepted — supersedes 0029 position/width |
| [0031](0031-paint-order.md) | Paint order buried three correctly-positioned details | Accepted |
| [0032](0032-note-removal-menu-confirm.md) | Note text removed; menu and keyboard toggle confirmed shipping | Accepted |
| [0033](0033-stamp-photo.md) | Stamp-S3A label is a photograph on its own route | Accepted |
| [0034](0034-adv-label-css.md) | CARDPUTER ADV label stays CSS, measured against the photo | Superseded by 0035 |
| [0035](0035-adv-label-photo.md) | CARDPUTER ADV label becomes a photograph; CSS screws removed | Accepted — supersedes 0034 |
| [0036](0036-remove-sd.md) | Remove the SD card feature entirely | Accepted — supersedes 0008 |
| [0037](0037-control-over-wifi.md) | Why control appeared to need USB: modem sleep, and a counter that lied | **Accepted — implemented** |

This table covers only the four mirroring/control strategy options; ADRs
0005-0037 (wifi manager, on-device menu, keyboard layout, etc.) live in this
directory but aren't part of that comparison.

## Library API

| ADR | Title | Status |
|-----|-------|--------|
| [0038](0038-adapter-driven-begin.md) | Adapter-driven `begin()`: `IInputSink` / `IHostAdapter` — lets a second host firmware (see [launcher-adv-mirror](https://github.com/ry-ops/launcher-adv-mirror)) supply its own frame source and input path without editing this library | Proposed |
| [0039](0039-driver-independent-readback.md) | `SpiReadbackFrameSource` — GRAM readback for hosts without M5Unified/M5GFX | Accepted — implemented, unverified on hardware |
| [0040](0040-async-webserver-fork.md) | `esp32async/ESPAsyncWebServer` instead of the esphome fork — the esphome fork can't build against an arduino-esp32 3.x core | **Accepted — implemented** |
| [0041](0041-frame-source-failure-is-not-fatal.md) | A frame source failure doesn't take down remote control — server/WS/key injection no longer depend on `_src->begin()` succeeding | **Accepted — implemented** |

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
