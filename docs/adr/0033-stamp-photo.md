# ADR 0033 — Stamp-S3A label is a photograph on its own route

## Context
The sticker had been a CSS rainbow gradient with the text "STAMP S3A" — openly
decorative. The real module label carries the pinout the user actually needs:
GPIO numbers, the BTN/DOWNLOAD-MODE and USB legends, and the bus-pin column
(SCK/MISO/MOSI, SDA/SCL, LCD.PORT). This is a tool for talking to those pins,
so drawing plausible-looking artwork would put wrong pin numbers on screen.
The user chose the photo route over CSS.

## Decision
Crop the label from the product photo and serve it as `stamp.webp`.

### Why a separate route, not a base64 data: URI
Base64 costs 33% on bytes that are ALREADY compressed, and the result is
incompressible, so inlining would add ~22 KB of dead weight to the gzipped page
on every single load. As its own resource it is cached (`immutable`, 1 year)
and the page stays at 18.9 KB gzipped. `gen_web_assets.py` now emits a second
PROGMEM array, `kStampWebp`, and `CardputerMirror.cpp` serves it with no
`Content-Encoding` — WebP is already compressed and re-gzipping it would only
burn CPU on a device budgeted in ms per tile.

### Why 263x188 native and not upscaled
The crop is 263 px wide in the source photo. Encoding at 480 px stores
interpolation, not information: it costs 50% more bytes for pixels that carry
nothing the 263 px version doesn't. Quality 88 at native size is 22,204 B.

| size | webp q80 | verdict |
|---|---|---|
| 263x188 (native) | 17.7 KB | chosen, at q88 = 22.2 KB |
| 400x286 | 27.8 KB | upscaled, no new detail |
| 480x343 | 33.4 KB | upscaled, no new detail |
| 263x188 PNG | 105 KB | 5x the size for a photograph |

## Measurement
The box was measured off the module's black bezel, with the scan bounded to
the right of the glass. An unbounded dark scan locks onto the display edge and
returns x from 62.5% — the glass is darker than the bezel and adjacent to it.

Bounded: **x 68.39–99.18%, y 2.19–36.38%**. The old box (68.4 / 4.4 / 29.2 /
29.5) was eyeballed; it ran 2.2% short at the top and 4.7% at the bottom. Its
left edge was already right, which is an independent check on the measurement
rather than a coincidence worth ignoring.

`object-fit:fill`, not `contain`: the box is set to the crop's own aspect
(263/188 = 1.399), so there is nothing to letterbox, and `contain` would
silently absorb a future aspect mismatch instead of showing it.

## Consequences
Flash 31.8% -> 32.5%. Page gzip 18,502 -> 18,932 B.

Ten assertions added, and one existing check had to be fixed BEFORE it broke:
the suite parsed every `0x..` in `WebAssets.h` header-wide and compared the
count to `kIndexHtmlGzLen`. With a second array in the file that check would
have concatenated both and failed. The parse is now scoped to the
`kIndexHtmlGz` array by name.

The aspect assertion derives the case ratio from the stylesheet
(`height:calc(var(--cw)/1.5558)`) rather than duplicating the constant, and it
got the direction wrong on the first run: width% is of case width and height%
is of case height, so the pixel ratio is `(w%/h%) * caseAR`, not divided.
Dividing gives 0.579. Both directions look plausible written down, which is
exactly why the value is asserted rather than trusted.

## Still open
Unchanged from ADR 0031: whether the BtnG0/BtnRst top-edge tabs render. Needs
an uncropped screenshot.
