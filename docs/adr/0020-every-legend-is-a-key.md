# ADR 0020 — Every legend is a key: dual-legend caps and a solved-for layout

**Status:** accepted
**Context:** user: "we're still missing keys... we need each and every button
represented", followed by an explicit row-by-row enumeration.

They were right, and the enumeration was the useful part: it let me test against
a stated expectation instead of my own reading of the same source I had already
misread twice.

## Two defects, both invisible from the code

Measuring the rendered layout instead of reading it:

```
row0 14 keys  520px    ` 1 2 3 4 5 6 7 8 9 0 - = del
row1 14 keys  520px    tab q w e r t y u i o p [ ] \
row2 14 keys  548px    fn shift a s d f g h j k l ↑ ' enter
row3 14 keys  604px    ctrl opt alt z x c v b n m ← ↓ → space
primary legends visible : 56
secondary legends HIDDEN: 47  ->  ~ ! @ # $ % ^ & * ( ) _ + { } | : " < > ? ...
```

**1. Clipping.** `.krow` was an unwrapped flex row of fixed `min-width:30px`
keys. Row 3 needs 604px. In a narrower panel the tail simply overflowed out of
view -- no error, no scrollbar, the right-hand keys just were not there. The
count was always 56; the *visible* count was not.

**2. Only half the legends existed.** A physical ADV keycap prints **two**
characters. The renderer painted one at a time, gated on the shift latch, so 47
symbols were unreachable unless you knew to latch shift first and re-read every
cap. And my own ADR 0019 fix made this worse: the arrow legend *replaced* the
`;` it shares a cap with, so a key the user could legitimately enumerate
vanished. Fixing one legend by deleting another is not a fix.

## Resolution

**Print what the cap prints.** Each key renders its shifted legend (small, dim,
above), its primary legend, and -- for the arrow cluster -- the arrow as a
*third* mark in the corner. Nothing is replaced. 107 legends over 56 positions.

**Every legend is its own click target.** Clicking the upper legend sends the
coordinate with `shift:true` regardless of latch state, so a symbol is one click
rather than latch-click-unlatch. `sendKey(k, forceShift)` -- the device still
receives a coordinate plus a flag, exactly as a physical press reports.

**Solve for the unit; do not hardcode it.** `fitKeyboard()` computes the key
unit from the measured panel width so all 14 columns always fit:

    widest row = 13 units + one double-width space bar + 13 gaps
               = 15*unit + 14*GAP        (the wide key swallows a 14th gap)
    unit = floor((avail - GAP*14) / 15)

Clamped to 18..44px; below the 18px legibility floor the panel scrolls
deliberately rather than clipping silently.

My first version of that formula used 13 gaps and left the row ~1px over budget
at several widths. That surfaced as a scrollbar, not an error -- which is
exactly why the fit has to be arithmetic checked across widths rather than
eyeballed at one.

## Tests

- `tools/test_coverage.mjs` -- asserts the user's enumeration explicitly, group
  by group, against the legends the renderer will actually paint. It fails when
  a legend is *unrendered*, not merely when a coordinate is absent; the old code
  would have passed a coordinate-only check while hiding 47 symbols.
- `tools/test_dual_legend.mjs` -- for every dual-legend cap, the lower legend
  emits `(r,c,shift=false,lo)` and the upper emits `(r,c,shift=true,hi)`. Proves
  the second legend is functional rather than decorative.
- Fit arithmetic verified at 300..1200px.

All pass, alongside the existing vendor-header agreement test.

## Consequences

- Page 23,123 -> 26,279 B raw, 9,915 B gzipped. Flash 1,055,105 B (31.6%).
- The keyboard no longer resembles the device's proportions as closely, which is
  fine: the user's requirement was that every key be *available*, not that the
  layout be a photograph.

## The pattern, for the third time

ADR 0017: a hardcoded coordinate table produced a false test failure.
ADR 0019: reading the value map answered a different question than the one asked.
Here: the render path answered "does this key exist" while the user was asking
"can I press it".

Each time the artifact was correct in the dimension I checked and wrong in the
dimension I did not. The countermeasure that keeps working is to test against an
*externally stated* expectation -- the user's enumeration, the vendor header --
rather than against my own model of the same thing that produced the code.
