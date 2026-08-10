# ADR 0031 — Paint order buried three correctly-positioned details

## Context
A screenshot of the built page in a real browser showed the microphone missing
and one screw half-covered by the white label panel. Every one of those
elements had coordinates that matched the product photo to within a few
tenths of a percent. Nothing was mispositioned.

`#devlabel` is an opaque white panel emitted AFTER `#scr1`, `#scr2`,
`#devmic` and `#devgrille` in the markup. Painted last, it covered them.

## Decision
Emit `#devlabel` BEFORE the screws and grille, and keep `#devmic` after it.
That split is not arbitrary: viewing the label's lower edge at magnification
shows the mic icon is *printed on the label itself*, while the grille slots
and the left screw sit on bare plastic below the label, with the label's
bottom edge notched around the screw head.

Three measurements were corrected in the same pass:

| element | was | measured |
|---|---|---|
| label box | x 2.6-22.2%, y 5.3-32.8% | x 2.12-23.03%, y 2.74-31.08% |
| grille | y 26.0-28.6%, orange | y 31.81-35.47%, black |
| grille slots | CSS gradient | two `<i>` pills, w 29.9% each |

The old label box ran 2.2% too low at the bottom, which is what reached down
over the screw. The grille was in the wrong band entirely — at 26% it fell
*inside* the label, so it would have been invisible whatever colour it was.

The slots are black, not orange. The orange came from assuming they matched
the mic beside them; the darkest 15% of those pixels reads (0,0,0) against
(206,206,206) plastic.

## Measurement note
Isolating the label needed the row scan bounded above the keyboard strip.
That strip is also white, and an unbounded profile locks onto it — returning
a label bottom of 44.79%, which is the keyboard. Earlier attempts with
whiteness thresholding plus hole-filling returned the *screw* instead of the
panel, because the label's own printed text fragments it into pieces.

## Consequences
Eight assertions now cover this, and four of them assert ORDER rather than
geometry — `indexOf('devlabel') < indexOf('scr1')` and so on. A geometry-only
suite could never have caught this defect, since the geometry was already
right. That is the lesson worth keeping: for overlapping opaque elements,
position assertions are not sufficient.

## Still open
The BtnG0/BtnRst tabs do not appear in the screenshot. This is NOT resolved.
The shot is cropped to 8 device px above the case where the stylesheet
guarantees at least 72, and the page's h1 and subtitle are absent from it, so
the tabs may simply be outside the crop. Decompressing the built firmware
asset confirms the tab markup and CSS are in what the device serves, and no
clipping context, media query or margin collapse was found. Chrome and
QuickLook are both blocked by the sandbox here, so this cannot be settled
locally — it needs an uncropped screenshot.
