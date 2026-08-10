# ADR 0035 — CARDPUTER ADV label becomes a photograph; CSS screws removed

## Context
ADR 0034 kept the ADV label as CSS and argued the case: the S3A module label
carries printed pinout data that cannot be authored without inventing GPIO
numbers, whereas this one is two words and four registration brackets, which
CSS reproduces exactly and keeps sharp at any zoom. Every measurement in that
ADR was correct — bracket count and geometry, sampled bracket colour, cap
heights giving 0.0229cw and 0.0621cw, the 1.158 horizontal correction for M5's
wider face, the 38.79% text centre, the #e8e8e8 fill.

The user rejected it anyway, twice.

## Decision
Serve a photograph, `/advlabel.webp`, and delete the CSS reconstruction.

The argument in 0034 was not wrong about what it measured; it was wrong about
what makes a label read as real. What CSS could not carry is exactly what the
eye uses: M5's actual typeface, the slight ink spread of the printing, the
panel's surface texture, the soft shadow at its edge. Reproducing the correct
quantities is not the same as reproducing the artefact. The resolution argument
also weakened once the box was re-measured — the crop is 180x155 for a box that
renders at ~344 device px, so it is upscaled about 1.9x, which for a photograph
of a matte panel is acceptable where it would not be for text-bearing artwork.

The two CSS screws are removed at the user's request. Their rule and both nodes
are gone, and the test asserts their absence rather than their position, so
restoring them has to be a deliberate act.

## Measuring the box
The panel's fill is 232 and the surrounding plastic 214 — 17 levels apart — and
the plastic carries a brightness gradient that darkens toward the screen. Every
global approach failed on that:

  - threshold at >175 and take the largest component: leaked into the plastic,
    returning a box 3% too wide and 5% too tall
  - threshold at >225: clipped the left and top edges to the crop boundary,
    because the gradient puts the top-left plastic above the cut
  - darkest-point-in-band per row: found the crop's own falloff at the far
    right rather than the label's edge, drifting the right edge 1.8% out

What worked was keying on the border groove's signature — a local dark line
with BRIGHTER pixels on both sides, scored as `(p[i-3]+p[i+3])/2 - p[i]`. That
is a property of a groove specifically, and it is insensitive to the gradient
because both reference points move with it.

    x  1.880% .. 22.914%   (w 21.034%)
    y  2.559% .. 30.713%   (h 28.154%)

The old CSS box was 2.120–23.040% x 2.740–31.080% — close, but not measured.

## The screw in the crop
A case screw genuinely overlaps the panel's lower right in the product photo,
so the bottom-edge scan first returned row 181 rather than 168: the screw's own
dark ring outscored the groove on the columns it covers. Excluding those columns
put 20 of the remaining ones at row 168 and confirmed the edge.

Painting the screw out took four attempts, each rejected on inspection:

  1. dark-pixel mask — left the screw's bright chrome ring behind
  2. hand-guessed footprint from the case coordinates — left an arc outside it
  3. deviation-from-fill with closing — merged the screw with the microphone
     into one component, and the repair clipped the mic's stand
  4. component labelling below the wordmark — mic occupies cols 60-85, screw
     cols 99-132, they do NOT overlap in x. Repairing cols 93-152 from row 138
     covers the screw and leaves the mic untouched.

The fill is taken per-row from the label's left side rather than as one flat
colour, because the panel has the same gradient the plastic does.

## Consequences
- `object-fit:fill`, not `cover`: the box aspect is 1.1624 and the crop's is
  1.1613, so there is nothing to letterbox — and `cover` would crop the
  registration brackets the crop was cut to preserve.
- The generator now emits images from a list rather than a hardcoded pair, so
  a third asset is one line. The byte-parse in the test was already scoped to
  `kIndexHtmlGz` by name (ADR 0033), so it survives the extra array unchanged.
- Assertions target the retired CSS by name — `#devlabel i{`, `.l1`, `.l2`,
  `scaleX(1.158)`, `padding-bottom:22.42%`, `background:#e8e8e8`. Dead rules
  styling an `<img>` would be silently inert rather than visibly wrong, which
  is the harder thing to find later.
- The ADV block parses its own `caseAR` from the stylesheet. The Stamp block
  below has a `const caseAR` of its own, block-scoped and not visible here;
  the first attempt referenced it and threw a ReferenceError.

## The duplicate microphone
Removing the CSS label exposed a second defect. The mic is printed ON the label
and is therefore inside the crop, but `#devmic` also drew one as an element.
Their measured centres are 10.35%/25.99% (photo) against 10.46%/24.96% (CSS) —
about 1% apart, which reads as a smear rather than as two icons, and is why it
was not obvious. `#devmic` and its rule are removed; the photograph carries it.

This is the same class of error as ADR 0031's paint-order bug: both elements
were correctly positioned, and position is what the suite asserted. The check
that catches it is a NEGATIVE one — that no CSS mic exists at all — because
once the artwork carries a feature, any element drawing it again is wrong no
matter where it sits.
