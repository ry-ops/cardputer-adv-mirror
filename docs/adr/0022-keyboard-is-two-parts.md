# ADR 0022 — The keyboard is two parts: printed case legend + rubber dome

**Status:** Accepted — implemented
**Context:** user: "we're not using a standard keyboard layout here. this is one
of a kind. the onscreen keyboard needs to match the adv keyboard layout."

## What I had been rendering

One flat cap per key, in a 14-column grid, with the legend inside the cap. That
is a picture of the **scan matrix**, not of this keyboard. It would look
approximately right on any laptop, which is exactly the problem.

## What the ADV physically is (measured, not eyeballed)

Measuring the product photo instead of describing it:

| quantity | value |
|---|---|
| columns per row | 14, uniform |
| column pitch | 21.77 px (sd 0.31 across row 0; 0.23 across row 3) |
| dome width | 14 px = 64% of pitch |
| dome height | 9 px = 32% of cell height |
| cell aspect | 21.77 / 28.30 = 0.769 w/h |
| row 0 vs row 3 dome centres | agree within 1 px, all 14 columns |

Two structural facts fall out of that, and both were absent from my render:

1. **The legend is printed on the CASE, not on the key.** A row's legends form
   one continuous light bar (`#EBEBEB`); the movable part is a separate black
   rubber dome *below* it. A key is two parts.
2. **There is no wide key.** Row 0 and row 3 dome centres align to within a
   pixel, so space is exactly one unit. The double-width space bar in my earlier
   render was mine, not the board's.

Cap colours, sampled as the median of each legend strip (a brightest-quartile
estimator misread the small coloured caps and had to be replaced):

    del   (0,13)  #E6531F orange      Aa  (2,1)  #1F5C9E blue
    fn    (2,0)   #E6531F orange      opt (3,1)  #3FBA93 green
    enter (2,13)  #2C2C2C near-black  tab/ctrl/alt  #C7C7C7 grey
    everything else: bare case #EBEBEB

## The inference I got wrong, and what corrected it

`esc`, `del` and the four arrow glyphs are printed in the *same orange* as the
`fn` cap. I read that as a colour code meaning "fn-layer chord", and wired arrow
clicks to send `fn+key`.

That was wrong, and the project's own prior work says so. ADR 0019 established
the ADV has **four dedicated arrow keys** reporting `;` `,` `.` `/` because
M5Cardputer reuses the original keymap; `menu::handleKey` checks no modifier for
them, and the 56-key sweep lands them without one. `del` is orange too and is
plainly dedicated. Orange is the board's accent colour for special keys.

**Colour is weak evidence about wire protocol.** Sending `fn` would have made
the browser's press differ from a physical one. Reverted; the arrow legend is a
click target and a log label, never an added modifier.

## A defect only the render could show

Font sizing and legend rules were self-consistent and still wrong: 26 letter
caps printed a redundant blue uppercase secondary, because the rule was
`hi !== lo` and the vendor map stores `lo='q', hi='Q'` — the same glyph in the
other case. The physical cap prints one `Q`. Fixed to
`hi !== lo && hi.toLowerCase() !== lo.toLowerCase()`, which leaves exactly the
21 genuine symbol secondaries the coverage test independently counts.

The re-render then showed U+2334 (the space cap's counterbore mark) as a tofu
box — missing from DejaVu, and a risk in browser UI fonts too. Drawn from CSS
borders instead, so it has no font dependency.

**Fifth instance of the same pattern**: every check passed inside the frame I
chose, and the frame was the defect. Generating a picture is what breaks out of
the frame — so layout work is not done here until something has been rendered
and looked at.

## Top edge

The ADV's top face carries controls absent from every front-on photo: `BtnG0`,
an OFF/ON slide switch, the microSD slot, `BtnRst`, and the IR LED on G44. The
silkscreen's SD pins (SCK:G40 MISO:G39 MOSI:G14 CS:G12) match M5Unified's ADV
table exactly, which cross-validates the library against the hardware.

- **BtnG0** is the only top button firmware can observe — M5Unified registers a
  single button for `board_M5CardputerADV`. It is injected through
  `Button_Class::setRawState()`, which is the same entry point the library uses
  to feed real GPIO samples, so `wasPressed()` and the hold timers behave
  identically to a finger. It gets its **own sink** (`onBtn`), not a synthetic
  `(row,col)`: a GPIO is not a matrix coordinate, and inventing one would create
  the second vocabulary `keyinject.h` exists to prevent.
- **BtnRst** drives the EN line, cutting power to the SoC. Firmware cannot
  actuate it and `esp_restart()` is a different operation (warm reset, no power
  cycle). It is rendered **inert with an explanation** rather than given a button
  that silently does nothing — a control that lies about the hardware is worse
  than an absent one.
