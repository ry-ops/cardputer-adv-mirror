# ADR 0045 — Mobile-first pass: real sizing tiers, and typing via the phone's own keyboard

**Status:** Accepted — implemented, verified via `tools/test_dom_keyboard.cjs`
and on real hardware; native-iOS-keyboard behavior itself not yet confirmed
on a physical iPhone (see Consequences).
**Deciders:** firmware/dashboard owner
**Related:** ADR 0027 (hamburger menu — the button this ADR resizes and
repositions further), ADR 0017 (remote keyboard — the coordinate-lookup
tables this ADR's mobile relay reuses), ADR 0044 (the USB/serial debugging
this session also produced, unrelated to this change).

## Context

The dashboard had **zero `@media` queries** anywhere in its stylesheet.
Every size — `h1` at a flat `15px`, buttons/selects at `12px`, the hamburger
button at `padding:4px 9px` — was one value for every viewport. It looking
acceptable on an iPhone was coincidence: the ADV case mockup is genuinely
responsive (percentage-driven, measured against the product photo per ADR
0028 and friends), but the surrounding page chrome around it was not
designed for touch at all, just small by accident of using a monospace font
at desktop-scale numbers.

Separately, the existing "Capture my keyboard" feature (ADR 0017) works by
listening for `keydown` on `document`. That's sufficient for a physical
keyboard, but **iOS only shows its on-screen keyboard, and only dispatches
key/input events at all, once a real editable element has focus** — the
page had no focusable text field anywhere, so there was no way for a phone
to summon a keyboard to type with in the first place. "Zoom in on your
phone and actually type on it" was not just unpolished; it was impossible
with the page as it stood.

## Decision

**1. Mobile-first sizing.** Unprefixed rules are now the base (small-screen)
tier — `body` at `15px`, `h1` at `26px/700`, `button,select` at
`padding:9px 14px;font-size:15px` — sized for a thumb and a phone's reading
distance. A single `@media (min-width:640px)` block scales these back down
for a mouse-driven viewport, restoring roughly the old desktop density
(`13px`/`16px`/`12px`). This is the reverse of how the rest of the file's
CSS reads (percentage-of-case-width, scaling *up* with room), which is
exactly the point: text and controls need a floor a thumb can hit; the case
mockup already had one via its own measured-percentage system.

**2. Title.** `<title>`/`<h1>` shortened from "M5Stack Cardputer ADV —
Display Mirror" to **"ADV Mirror"**, at the user's request, paired with the
above type bump so it reads as an actual page title rather than shrunk body
text.

**3. Hamburger tap target.** `#menubtn` grew from an effectively ~23px box
(`font-size:15px;padding:4px 9px`) to a fixed `44x44` — Apple's own Human
Interface Guidelines minimum — at every breakpoint, not just mobile. A
bigger hit target costs a mouse user nothing, so there was no reason to
shrink it back down in the desktop tier the way text/buttons do.

**4. Zoom pops the display up ABOVE the case, not below it.** `#shell`
(the popped-out canvas host) moved earlier in DOM order, ahead of `#dev`
(the case mockup), so that when `setPopped(true)` un-hides it, it renders
above the mockup in the page's column flex layout instead of below it —
the reachable spot once an on-screen keyboard has eaten the bottom half of
a phone screen.

**5. Native keyboard capture via a hidden input.** Added `#mkbInput`, a
visually-hidden (`opacity:0`, not `display:none`/`visibility:hidden` —
those would make it unfocusable) `<input>` with
`autocomplete/autocapitalize/autocorrect="off"` and `spellcheck="false"`
(so iOS's text-prediction layer doesn't rewrite what's typed before this
page ever sees it) and `font-size:16px` specifically (iOS auto-zooms the
page on focusing any input under 16px, even an invisible one — the
computed size is what's checked, not visibility). `setCapture(on)` now
focuses/blurs it whenever `isTouch` (`matchMedia('(pointer:coarse)')`) is
true, which both the existing "Capture my keyboard" button and the new
zoom-triggered path (next point) drive through unchanged.

Two different signals are trusted for two different kinds of key, because
they're reliable for different reasons: `keydown` still fires normally from
iOS's on-screen keyboard for **Backspace / Enter / Tab / arrows** once the
field has focus, so those are handled exactly like the physical-keyboard
path. Ordinary **characters** are read from the resulting `input` event's
value instead of trusted from `keydown` directly, because autocorrect/
predictive text can insert or rewrite text between keystrokes on iOS; the
field is cleared after every `input` event so the next one only ever
contains what's new.

Both capture paths (physical `keydown` on `document`, and the new
`mkbInput` relay) now call one shared `sendSpec(spec)` instead of each
duplicating the `send()`+`noteSent()` pair — a genuine deduplication this
change surfaced, not scope creep.

**6. Zoom and capture are one action, but only on touch.** `$('zoombtn')`'s
click handler now also calls `setCapture(on)` matching the new popped state,
gated by `isTouch`. On a phone, tapping zoom both pops the display up *and*
focuses the hidden input in one tap — the whole point of the combo is to
get from "look at this" to "type on this" without a second control to find.
On desktop, zoom and capture stay independent, exactly as before: a mouse
user clicking zoom to look closer should not also start silently eating
their physical keystrokes.

## A bug this surfaced and fixed during implementation

`keydown` bubbles. Without a guard, every keystroke typed into `mkbInput`
would fire twice — once from `mkbInput`'s own listener, once again from the
document-level physical-capture listener it bubbles past. Fixed with
`if(!hwCapture || e.target===mkbInput) return;` in the document-level
handler: `mkbInput`'s own pair of listeners are the only handler for
anything typed there. Caught by re-reading the wiring before shipping it,
not by a test — worth being honest that `test_dom_keyboard.cjs` (jsdom) is
not equipped to catch a double-send bug like this, since jsdom's `keydown`
dispatch and bubbling behavior isn't what's in question; a real double-fire
here would need a live/manual check to catch, and hasn't been manually
verified on an actual iPhone yet (see Consequences).

## Consequences

**Positive**

- The dashboard now has a real mobile/desktop sizing split instead of one
  set of numbers that happened to be tolerable on both.
- Zoom-in-and-type is now technically possible on a phone at all, which it
  was not before this change regardless of how the on-page graphical
  keyboard was styled.
- The `sendSpec` extraction removed real duplication between the two
  capture paths rather than adding a third parallel one.

**Negative / open**

- **Not yet verified on a physical iPhone.** Everything here is verified
  against `tools/test_dom_keyboard.cjs` (jsdom — confirms structure/wiring,
  not real iOS keyboard event behavior, which jsdom cannot simulate at all)
  and against real hardware for the CSS/layout side (flashed and visually
  confirmed at a normal desktop viewport). The core claim this ADR rests
  on — that iOS reliably fires `keydown` for Backspace/Enter and `input`
  for characters once `mkbInput` has focus — is standard, documented iOS
  Safari behavior, but this session had no working way to drive a real
  mobile viewport through the available Chrome automation (`resize_window`
  did not actually change the tab's effective viewport here — `innerWidth`
  stayed fixed regardless of the requested size), and no physical iPhone
  in the loop. Needs a real on-device check before this is trusted fully.
- iOS's stock phone keyboard has no Tab/Escape/arrow keys at all (those
  only exist via iPad/external keyboards), so most of point 5's non-Enter/
  Backspace branches are dead code on an iPhone specifically, kept for
  other touch devices rather than removed.
- If focus is lost off `mkbInput` some other way (the user scrolls, taps
  elsewhere, backgrounds Safari), capture silently stops working until they
  re-tap zoom or "Capture my keyboard" — no automatic refocus was built,
  deliberately, to avoid fighting the user's own taps with a script that
  keeps stealing focus back.

**Neutral**

- The `min-width:640px` breakpoint is a single guess at "phone vs.
  everything else," not measured against a device matrix the way the case
  mockup's percentages are. Fine to move if a real tablet-width case shows
  it's wrong.

## Alternatives considered

- **Keep `keydown`-only capture for `mkbInput`, skip the `input`-event
  diffing.** Rejected: simpler, but character keys would be lost or
  duplicated any time iOS's autocorrect/predictive layer touches the field
  between keystrokes, which is common with autocorrect literally impossible
  to fully suppress on iOS regardless of the `autocorrect="off"` attribute
  (it reduces but doesn't eliminate predictive behavior).
- **A separate `isTouch`-only capture flag, independent from `hwCapture`.**
  Rejected: would have meant two parallel "am I capturing keys" states to
  keep in sync (badge visibility, menu button label, Escape-yields-to-device
  logic) instead of one. Routing mobile through the same `hwCapture`/
  `setCapture` the physical path already uses kept every existing capture-
  aware call site correct for free.
- **Build the "wait, is the phone still focused" refocus logic now.**
  Rejected as premature: no evidence yet (no real-device test performed)
  that focus loss is actually a practical problem worth the complexity of
  babysitting it.
