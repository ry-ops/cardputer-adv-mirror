# ADR 0021 — The keyboard collapsed to one column: a self-referential CSS measure

**Status:** Accepted — implemented
**Context:** user screenshot showed the keyboard rendered as a ~30px vertical
sliver of overlapping fragments, plus a product photo of the intended layout.

## Cause: I measured the thing I was sizing

ADR 0020 introduced `fitKeyboard()`, which computed the key unit from
`kb.clientWidth`. But `body` is `display:flex; align-items:center`, so `#kb` is a
flex **item** -- it shrinks to fit its *content*. Its width is therefore a
function of the unit being computed from it:

    unit  <- f(kb.clientWidth)
    kb.clientWidth <- g(unit)

A feedback loop with a floor. It latched at the `18px` minimum, and
`overflow-x:auto` -- added in the same commit to "prevent clipping" -- turned the
resulting overflow into a hidden scroll region, so 13 of 14 columns were simply
not visible. The two changes were individually defensible and jointly fatal.

The prior turn's verification passed because it re-implemented the arithmetic in
Node and confirmed *the formula*. The formula was right. The **input** was wrong,
and no amount of checking the formula could reveal that.

## Fix

- `#kb` is `display:inline-flex; width:max-content; max-width:100%` -- it sizes
  to content deliberately, and `overflow-x:auto` is gone.
- `fitKeyboard()` measures `document.documentElement.clientWidth` -- the viewport
  is an **independent** quantity, not one downstream of the unit. This is the
  actual fix: breaking the circularity, not adjusting constants inside it.

## Layout, from the product photo

The reference shows a uniform aligned grid, not proportional rows. Changed
`.krow` from `flex` to `grid-template-columns:repeat(14, var(--ku))`, so columns
align down the board and every cap is the same size -- including space, which on
the real ADV is a single unit like the rest. Fit arithmetic simplified to
`14 units + 13 gaps` accordingly.

Secondary legends moved to a **top-right superscript**, matching the silkscreen;
arrows sit bottom-left so the two never collide. Shift's cap now reads `Aa`,
which is what the device actually prints -- `shift` was my wording. Safe because
latching keys off the `mod` field, never the visible label.

## The defect only a render could show

With fonts sized by *key role* (`.mod`, `.wide`), `enter` -- neither a modifier
nor space -- kept the full-size font and **overflowed its cap**. My width check
had cleared it, because I only fed that check the labels the rule targeted:
it confirmed the rule was self-consistent, not that it covered every key.

Sizing now follows **label length** (`l2`/`l3`/`l5` classes), which is the
property that actually governs whether text fits; role was a proxy that merely
correlated. A generated static render of the real geometry
(`keyboard_layout.png`) showed the overflow immediately.

Headless Chrome aborts under this sandbox (rc -6), so the preview is drawn from
the keymap using the same box model the CSS applies. Not a browser, but it makes
overflow visible, which arithmetic did not.

## Consequences

- Page 26,533 -> 27,018 B raw, 10,163 B gzipped. Flash 1,055,345 B (31.6%).
- All three tests still pass. `test_coverage.mjs` caught the `shift` -> `Aa`
  rename on the same run that introduced it, which is the test working.

## Pattern, fourth instance

0017 hardcoded coordinates; 0019 read the value map for a question it cannot
answer; 0020 checked existence while the user asked about reachability; here I
verified a formula while its input was wrong.

Each time the check was sound *within the frame I chose*. The recurring failure
is frame selection, and the cheapest correction has consistently been to
generate an artifact I can look at -- a render, a coverage table -- rather than
another assertion derived from the same model that produced the bug.
