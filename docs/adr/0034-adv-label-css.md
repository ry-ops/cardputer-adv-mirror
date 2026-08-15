# ADR 0034 — CARDPUTER ADV label stays CSS, measured against the photo

**Status:** Superseded by 0035 — the user rejected the CSS route after this ADR shipped it

## Context
The user asked whether the ADV label on the left of the case could be improved,
having just accepted the photograph route for the Stamp-S3A module (ADR 0033).
The obvious move was to crop this label from the photo too, for consistency.

## Decision
It stays CSS. The two labels are not alike:

* The S3A label is dense printed pinout data. It cannot be authored without
  inventing GPIO numbers, and this is a tool whose job is talking to those
  pins, so a photograph is the only honest option.
* This label is two words and four registration brackets. CSS reproduces that
  content EXACTLY, and reproduces it sharp at any zoom. The available crop is
  177x156 px -- it would go soft the moment the case renders wider than that,
  which it does at the default 820px cap (the label is 172 CSS px, 344 device
  px on a 2x display, nearly 2x the crop's native width).

Consistency with ADR 0033 would have cost resolution for no gain in accuracy.

## What was wrong, and what the photo says
Four defects, all found by measuring the crop rather than looking at it:

1. **Two brackets, not four.** The rendering used `::before`/`::after`, which
   can only ever supply two. The photo has FOUR: a pair at the top and a pair
   at y 60.26-67.95% of the label. The lower pair is not a corner mark on the
   panel -- it FRAMES THE TEXT, which is why it sits well above the bottom
   edge, leaving the lower third clear for the mic. Now four `<i>` elements.
2. **Brackets 2x too large.** Each is 6.78% x 7.69% of the label with an arm
   stroke 38% of its length. The old pair was 14% x 16%. Colour is (36,199,235)
   sampled, not the #21b6e8 that had been assumed.
3. **ADV wordmark 21% too small.** Cap-heights measured off the crop:
   CARDPUTER 8.97% of the label's height, ADV 24.36%. Converting through the
   case aspect (label height = 18.22% of case WIDTH) at ~0.715em cap-height
   gives 0.0229cw and 0.0621cw. ADV had been 0.049cw.
4. **Panel pure white.** Fill measured between the text rows is #e8e8e8 -- the
   real label is barely brighter than the #e5e7e5 plastic around it. #fff
   glared.

## Two corrections that needed a second measurement
**Width.** Sizing type by cap-height alone assumes the face's proportions.
Checking width-over-cap against the photo: CARDPUTER wants 10.20 and the system
stack gives 10.07, 1.3% off, left alone. ADV wants 3.596 against 3.105 -- 16%
narrow, because M5's wordmark face is wider than any system stack. Corrected
with `scaleX(1.158)`, which fixes width WITHOUT touching cap-height; raising
font-size would have made it too tall as well.

**Vertical position.** The text block spans 17.95-59.62% of the label, centred
at 38.79%. Flex centres it at 50%. `padding-bottom:22.42%` (twice the 11.21%
offset) raises the centred block by exactly that much.

## Consequences
12 new assertions. Four assert bracket COUNT and class, which is the defect a
geometry check could not have caught -- the two brackets that did exist were
positioned correctly. The 1.158 and 22.42% constants are asserted because both
are derived numbers that look arbitrary in the stylesheet and would be
"cleaned up" by anyone who did not know they were measured.

Page 20,131 B gzipped (+350 B), flash 32.6%.

## Also settled here: the top-edge tabs DO render
Open since ADR 0029 and unresolved across four spans. The user's dashboard
screenshot, taken over WiFi from the running device, resolves it. Contrast-
stretching the 8-pixel band above the case shows two soft glows centred at
x 194-270 and 878-955 device px. Predicted from the stylesheet at this
viewport: 34 CSS px wide (the hittability floor, since 0.0552cw = 33.9) plus
2px side shadows either side = 76 device px, at exactly those two positions.
Observed 76 and 77. The match is not a coincidence.

They read dark -- (32,34,39) against a (18,21,26) page -- because the shot was
taken with the browser window's own shadow over that band, so the tabs are
dimmed rather than absent. The geometry is right and the elements paint. No
change made.
