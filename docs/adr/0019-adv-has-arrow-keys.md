# ADR 0019 — The ADV has dedicated arrow keys; I said it did not

**Status:** Accepted — implemented
**Context:** user: "the adv does have arrow keys". They were right.

## The false claim

Shipped in the web UI, in a firmware comment, and repeated in chat:

> the ADV has no dedicated arrow keys; the menu reads `fn` + `;` `.` `,` `/`

Wrong. The ADV has **four dedicated arrow keys** in a physical inverted-T,
silkscreened with arrows. No modifier is involved.

## How I got there

`_key_value_map` in M5Cardputer holds `';' ',' '.' '/'` at those positions, and
there is no `KEY_UP`/`KEY_DOWN` macro anywhere in the library. I concluded the
keys did not exist. What actually happened is narrower and more interesting:

`TCA8418.cpp` scans a **7x8** matrix and then calls `remap()`, whose comment
reads *"Remap to the same as cardputer"*. The ADV's physical wiring is folded
onto the **original Cardputer's** 4x14 value map so one library serves both
boards. The classic Cardputer had punctuation there; the ADV put arrow keycaps
on those switches. **The legend and the reported character differ by design.**

The library therefore cannot tell you the ADV has arrows -- it is not modelling
keycaps, it is modelling a matrix. Reading only the value map answers "what
character does this key report", which is a different question from "what key is
this", and I answered the second with evidence for the first.

## What made it provable

Two checks, neither of which needed the physical device:

1. **Bijection.** Replaying `remap()` over all 7x8 = 56 raw positions yields
   exactly 56 distinct logical cells, zero collisions, covering the full 4x14
   grid. So there is no 57th key hiding anywhere -- the arrows must be among
   these 56, and the only question was which.
2. **Geometry.** `;`=(2,11), `,`=(3,10), `.`=(3,11), `/`=(3,12). Up sits
   directly above down; left and right flank down. That is an inverted-T --
   the universal arrow-cluster shape, and not a shape punctuation lands in by
   accident.

The user's report plus that geometry is conclusive. I should have run check 2
before writing the claim; it costs nothing and it is the *shape*, not the
character, that identifies an arrow cluster.

## The bug the false belief caused

The browser sent `fn: true` with every arrow press -- a modifier **the hardware
never asserts for these keys**. Harmless only because `menu::handleKey()`
happens not to test `fn`. Any future firmware that keyed on `fn` would have
rejected every remote arrow while physical arrows worked perfectly: a
divergence between the two input paths, which is exactly what ADR 0017's
send-coordinates-not-characters rule exists to prevent. A wrong belief in a
comment had leaked into wire data.

## Fixes

- `cap` field in the generated keymap carries the keycap legend; keys render
  `↑ ← ↓ →`. The key still **sends its coordinate**, so the label is
  presentation only and cannot desynchronise from what the firmware decodes.
- `fn: true` removed from the arrow path.
- Key Test names arrows by **coordinate**, not character: `';'` at (2,11) is the
  up arrow, but `';'` produced any other way is not. The coordinate is the only
  unambiguous discriminator.
- Help text and firmware comments corrected, with the old claim named as wrong
  so a future reader does not re-derive it from the value map.

## The guard, and why it is coordinate-based

`ARROW_CAP` is a hardcoded coordinate table -- precisely the thing this project
has been burned by (ADR 0017: hardcoded arrow coordinates in a test that then
reported failures against correct code). So `gen_keymap.py` verifies each of the
four cells still decodes to the character expected, **through `lit()`, the same
decoder the emitter uses**, and refuses to emit otherwise.

Verified by mutation: changing one expectation makes the generator exit 1 with
the coordinate named. A guard that has never been observed to fire is not known
to work.

Placement mattered: my first attempt compared raw C tokens (`"';'"`) against
characters (`";"`) and fired on correct input. Moving it below `lit()` was the
fix -- comparing through the emitter's own decoder means the guard cannot pass
while the emitted layout is wrong.

## Note on verification

My binary check reported `fn=true` still present (it was my own explanatory
comment) and `UP` absent (`strings` suppresses 2-char literals; a NUL-terminated
byte search found all four: `RIGHT\0LEFT\0UP\0`). Both were defects in the
check, not the build. A verification that cannot distinguish code from comment,
or absence from below-threshold, is not yet a verification.

## Consequences

- `+580 B` flash. Page 21,945 -> 23,123 B raw, 8,702 B gzipped.
- `w/s/a/d` aliases kept -- harmless, and useful over a serial terminal.
- Arrows need no `fn`. They never did.
