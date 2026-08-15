# ADR 0028 — Device mockup with inset display, zoom pops it out

**Status:** Accepted — implemented

## Context
The page showed a bare canvas above a detached keyboard. The request was a
single visual: the ADV case with the display inset and the working keyboard in
place, plus a zoom control that pops the display out to the large view.

## Decision

### Case is drawn, not photographed
Chosen over a photo background. A photo costs ~68 KB of flash, locks the aspect
ratio, and — decisively — its own printed keys sit under the live ones, so any
misalignment shows as doubled keys. Drawn CSS costs ~4 KB and lets the existing
keyboard drop in pixel-aligned.

### Every proportion is measured, not eyeballed
From the 1200x1200 front-on product photo. Case bbox 851x547 -> aspect 1.5558.
Fractions of case: glass left .2479 top .0530 w .2973 h .2779; keys left .0306
top .4004 w .9365. Cell h/w 1.3189, legend .4805 of cell height, dome .6475
of cell width. Sampled colours: case #e5e7e5, lower case #d3d3d3, legend
#e1e7e3, dome #1e1e1e, bezel #0a0c0b.

Two findings worth keeping:
- 14 domes across row 0, pitch 58.38px, **sd 0.35** — no key is wider than any
  other. This independently re-confirms ADR 0022: there is no wide space bar.
- The glass module is 253x152 (aspect 1.664) but the panel is 240x135 (1.778).
  The physical bezel is **not symmetric**. The canvas fits by width and centres,
  which is what the hardware does. This is not a measurement error.

### The canvas MOVES between hosts; it is never duplicated
`setPopped()` calls `host.appendChild(cv)` — one node, two possible parents.
Rejected: a second live canvas in the mockup with both updating. That roughly
doubles the per-frame blit on a stream whose entire design is a millisecond
tile budget, and the two copies diverge whenever one misses a tile.
The DOM test asserts `querySelectorAll('canvas').length === 1` at every stage
and that `getElementById('c')` is the *same node* after a full zoom cycle —
a recreated element would drop its 2d context and silently freeze the stream.

The zoom *select* is disabled while inset, because the inset canvas is sized by
the glass rectangle, not by a scale factor.

### --ku is driven by case width, and its ceiling is gone inside the mockup
The grid tracks the case automatically (`#devkb` is 93.65% of case width,
`.krow` uses `1fr`), but `--ku` still sizes the legend fonts, so it is derived
from `getBoundingClientRect().width` of the case. The old `Math.min(46, ...)`
ceiling was tuned for a viewport-driven keyboard; measured against the case it
clamps at **every** viewport >=768px while the case stays 820px — undersized
legends inside full-size caps. Its original job (bounding an unbounded
keyboard) now belongs to the `--cw` cap. The 16px floor stays: below that a key
stops being clickable. `fitKeyboard` is re-run in `requestAnimationFrame`
because case width is unknown before layout.

## Defect caught by rendering, not by arithmetic
The zoom button was first placed in the gap between the deck and the keyboard.
Every assertion passed. Rendering the measured proportions showed it colliding
with the number row: that gap is 3.34% of case height (17.6px at the 820px
maximum) and the button is 19.8px there — it overflows at **every** viewport
width tested, 320 through 1024, not just narrow ones.

It now sits on the bare deck strip between the glass (ends 54.4%) and the S3A
sticker (starts 68.4%) — 14% of case width — below the M5 logo band. Verified
clear of the deck edge at 320/480/1024px.

This is the fourth layout defect in this project that passed every numeric
check and was caught only by generating a picture. jsdom has no layout engine;
it can prove structure and behaviour, never geometry.

## Consequences
Flash 31.7% -> 31.8%. Test suite 30 -> 47 assertions.
Still unverified in a real browser: how the case reads at phone widths, and
whether the drawn plastic gradient holds up on an OLED display.
