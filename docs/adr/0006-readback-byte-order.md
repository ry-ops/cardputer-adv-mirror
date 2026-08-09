# ADR 0006 — The 25% self-test was a byte-order bug, not unreliable readback

**Status:** Accepted · 2026-08-08
**Supersedes the "escalate to ADR 0001" trigger in ADR 0002.**

## Context

First flash of the mirror firmware produced a readback self-test score of **25%**
on the device's own screen. ADR 0002 had pre-committed to treating a low score as
evidence that 3-wire GRAM readback is unreliable on this unit, and to escalating
to ADR 0001 (physical panel tee). That escalation would have been wrong.

## Evidence

Two independent observations, neither of which is consistent with "unreliable readback":

1. **The score was *exactly* 25%, not approximately.** The self-test pattern uses
   four colours in equal quarters: `0xF800, 0x07E0, 0x001F, 0xFFFF`. A flaky
   electrical path yields a noisy fraction. Exactly one-quarter means exactly one
   colour passed. Under a byte swap, `0xFFFF` is palindromic and survives; the
   other three do not. Predicted score under byte-swap: **25%**. Observed: **25%**.

2. **The mirror rendered the wrong colour, deterministically.** `src/main.cpp:61`
   draws the banner with `TFT_GREEN` (`0x07E0`). The browser PNG export contains
   only three distinct colours, and the banner pixels measure `#E70039`
   (RGB 231,0,57) across all 1,152 of them. Byte-swapping `0x07E0` gives `0xE007`
   = RGB(224,0,56) — matching to within RGB565 quantisation. A red/blue channel
   swap would instead give `0x07E0` unchanged (still green), so `swapRB` is ruled
   out. The error is a **byte** swap, not a **channel** swap.

Uniform, exactly-predictable corruption is a data-format defect. Noise is not.

## Root cause

`LGFXBase.cpp:1759`, in the `uint16_t*` overload of `readRect()`:

```cpp
void LGFXBase::readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data)
{
  pixelcopy_t p(nullptr, swap565_t::depth, _read_conv.depth, false, getPalette());
```

The overload is specified in terms of **`swap565_t`**, not `rgb565_t`. Our buffers
are `uint16_t*`, so this overload was selected by C++ overload resolution, and every
pixel arrived byte-swapped relative to everything the write path produces. The
comment in `fetchTile()` asserting it "converts into RGB565 for us" was wrong about
which 565 it meant.

## Decision

Cast the destination to `lgfx::rgb565_t*` at both call sites, selecting the
template overload (`LGFXBase.hpp:641`) which converts from `_read_depth`
(`rgb888_3Byte`) into true RGB565:

```cpp
M5.Display.readRect(tx, ty, kTileW, kTileH, (lgfx::rgb565_t*)dst);
```

Applied to `fetchTile()` and `selfTest()`. Cost: +40 bytes of flash, no RAM change,
no wire-format change, no change to the browser UI.

`setSwapBytes(true)` was rejected as an alternative: it is global display state that
would silently alter unrelated drawing, whereas the cast is local to the two reads
that need it.

## Consequences

- **ADR 0002 stands. Do not escalate to ADR 0001.** GRAM readback over the
  3-wire path was never shown to be unreliable; the one measurement that suggested
  otherwise was measuring LGFX's byte order.
- Expected self-test after this fix: **100%**, since all four pattern colours now
  survive. A score materially below that is new information and *would* re-open
  the ADR 0001 question — but the 25% reading must not be cited as that evidence.
- The `swapRB` runtime toggle in the browser remains useful for genuine panel
  revision differences, but is not the fix for this and must not be used to mask it.

## Lesson

A pre-committed escalation trigger is only as good as the assumption that the
metric measures what it claims. The self-test was written to measure GRAM readback
fidelity; because it read through the same defective overload as the mirror path,
it measured byte order instead. The tell was the suspiciously exact 25% — a
diagnostic that returns a clean fraction of the pattern size is describing the
pattern, not the hardware.
