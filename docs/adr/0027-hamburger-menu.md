# ADR 0027 — Move occasional controls behind a hamburger; keep modal state visible

**Status:** Accepted — implemented
**Context:** user asked to move `Full refresh`, `Swap R/B`, `Invert`, `Save PNG`,
the five stream stats, `Capture my keyboard`, `Test all 56 keys`, and the
`mods`/`sent`/`coverage` readouts into a hamburger menu.

## What stayed in the bar, and why

Not everything listed moved. The split is **"can you afford to click for it?"**

Stayed visible: connection dot, `zoom`, and the frame **budget** — budget
changes what you are watching while you watch it, so it belongs next to the
canvas.

Moved: everything else the user named.

**`Capture my keyboard` moved, but its STATE did not.** Capture rewires the
user's physical keyboard so keystrokes go to the device instead of the browser.
A mode that silently swallows input, whose only indicator is inside a closed
menu, is how you type into the wrong window. So an always-visible `#capbadge`
appears when capture is on, with its own stop button. The menu holds the
*control*; the bar holds the *state*.

## `mods none` was deleted, not moved

`.key.mod.on .lgd` already draws an amber ring on the latched cap. The text
readout restated it in a second place, and the cap is where the user is looking
when they latch shift. `renderMods()` lost the line that wrote it — that line
would have thrown `TypeError` once the element was gone, since `$()` returns
null.

## Dismissal is the failure mode

A menu that opens and cannot be dismissed strands itself over the keyboard —
the thing the user is reaching for. So: outside click closes, Escape closes,
clicks inside do not (via `stopPropagation`).

**Escape is yielded to the device while capturing.** With capture on, Escape is
a key the firmware expects; stealing it for the menu would make the browser
eat a keystroke. Guarded by `!hwCapture` and asserted in the test.

## Two layout defects found by measuring, not by looking

The mock render looked fine. Computing the panel height against real viewports
found both:

- ~270px of content **clips below the fold** on a 568px-tall phone, with no
  indication the Stream numbers exist. Fixed: `max-height:calc(100vh - 140px)`
  plus `overflow-y:auto`.
- 250px fixed width **overhangs the left edge** at ~320px screen width, because
  the panel grows leftward from a right-anchored button. Fixed:
  `max-width:calc(100vw - 24px)`.

Neither is visible in jsdom (no layout engine) and neither would have been
caught by the assertions — they came from arithmetic on the CSS.

## Test

`test_dom_keyboard.cjs` gained 25 assertions covering behaviour, not markup:
open, aria-expanded, outside-click close, inside-click no-close, Escape close,
Escape yielded while capturing, badge outside the menu, badge on/off, stop
button — plus one assertion per moved control, so silently dropping a control
during a future refactor fails the suite.

Writing them, I referenced a `win` binding that does not exist in that file;
the window is only reachable as `dom.window`. Same class of error as the
invented `buildKb` in ADR 0025: **read the file, do not trust the summary.**
