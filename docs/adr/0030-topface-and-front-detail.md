# ADR 0030 — Top-face photo corrects the button sides; front-face detail measured

**Status:** Accepted — implemented (supersedes 0029's position/width decisions)

## Context
ADR 0029 placed BtnG0/BtnRst from the documented top-edge order (G0, slide,
microSD, BtnRst, IR) because the only reference photo was front-on. A second
photo of the top face (7_f8c26a68-*.webp, 1200x1200) now provides real
geometry.

## The mirroring rule
The top-face photo is MIRRORED relative to the front view. Two independent
landmarks establish this:
  * the S3A pinout sticker spans 4.3-30.6% of case width in the top photo,
    but 68.4-97.6% in the front photo (1 - 0.306 = 0.694 checks out)
  * the silk-screened "CARDPUTER ADV" text reads reversed
So top-view x maps to front-view as (1 - x). Any future measurement taken
from a top or bottom face must be inverted before it enters the front-view
stylesheet.

## Consequence: the buttons were on the wrong sides
Reading the doc's top-edge order straight into a front view reverses it.
Measured, corrected:
  BtnG0   x 71.08-76.61%  (was 13%)
  BtnRst  x 15.26-20.78%  (was 71%)
Cap width is 5.52% of case width for both (was a guessed 6.2%). Caps are
#c4c4c4 seated in a near-black slot with a raised #dcdedd collar; the tab
styling now reflects that instead of the invented white gradient.

Cap HEIGHT is deliberately not taken from this photo: its vertical axis is
device thickness, not front-face height, so a height fraction measured there
would be meaningless in the front view. The max(22px, 2% of caseW) floor from
ADR 0029 stands.

## Front-face detail
Measured from the front photo, all fractions of the case bounding box:
  screws   cx 15.16% and 64.75%, cy 32.81%, diameter 3.47% of case width
  mic icon x 9.87-11.05%, y 22.85-27.06%, colour #f36732
  grille   two vertical slots centred cx ~10.4%, y 26.2-28.8%
  brackets cyan registration marks on the silk label, x 2.59-22.21%,
           y 4.57-21.76%

Screw detection needed two attempts. Thresholding for dark blobs found
nothing: the hex head is CHROME and reads brighter than the surrounding
plastic, with only the recess ring darker. Detecting bright heads enclosed by
a darker ring found three candidates; viewing each at magnification confirmed
two as screws and rejected the third (94.71%) as sticker pin graphics.

## The sticker is not reproduced
The S3A label is a dense multicolour pinout map — GPIO numbers, regulatory
marks, a QR-like block. It is rendered as a tinted panel with a colour band
across the top. Below roughly 200px of case width its real text is unreadable
anyway, and inventing plausible-looking pin labels would put wrong pin numbers
on the screen of a tool whose whole purpose is talking to that hardware.

## Status
Supersedes the position and width decisions in ADR 0029. The protrusion
decision and the hittability floors in ADR 0029 stand unchanged.
