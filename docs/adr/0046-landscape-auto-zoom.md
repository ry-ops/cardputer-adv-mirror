# ADR 0046 — Landscape auto-zoom: an iPhone-only toggle that pops the display up and fills the screen

**Status:** Accepted — implemented, not yet flashed/verified on hardware
(see Consequences).
**Deciders:** firmware/dashboard owner
**Related:** ADR 0045 (mobile-first pass, native keyboard capture — this
reuses `isTouch`/`setPopped()` from that work but deliberately does NOT
reuse its zoom+capture coupling); ADR 0027 (hamburger menu — where the
toggle lives).

## Context

Prompted by a proof-of-concept idea: mount the device somewhere (e.g. a car
dash, while wardriving), and AirPlay-mirror the phone's own Safari tab
showing this dashboard to a stereo/display. The AirPlay leg needs nothing
from this project — it's the phone mirroring its own screen — but the
dashboard itself had no notion of "I'm mounted sideways, make the display as
big and legible as possible" at all. The canvas is already natively 240x135,
which is exactly 16:9, so no aspect-ratio change was needed — only a way to
present it that large automatically.

## Decision

A toggle in the hamburger menu's Display section, **"Auto-zoom in
landscape,"** off by default. The row itself (`#autozoomRow`) is hidden
entirely on any device that isn't an actual iPhone
(`/iPhone/.test(navigator.userAgent)` — iPadOS reports as desktop Safari by
default and has no "iPad" in its UA, so this string match means what it
says, not "any Apple touch device").

When the toggle is on and `matchMedia('(orientation:landscape)')` matches,
`setPopped(true)` runs automatically, and the canvas is sized by a new
`fitLandscapeCanvas()` that fills the available viewport (width capped by
height, preserving the native 240:135 ratio) rather than using the existing
zoom SELECT's fixed 2x-5x steps — a `landscapeFill` flag distinguishes this
sizing strategy from manual stepped zoom so the two don't fight over the
same `zoom()` function. Rotating back to portrait, or turning the toggle
off, reverses it.

**Deliberately NOT coupled to `setCapture()`.** ADR 0045 ties the *manual*
zoom button to keyboard capture on touch devices, because tapping zoom
there is explicitly "I want to type." This feature's use case is the
opposite — glancing at a mounted, unattended phone while driving — so
auto-popping the iOS keyboard open on every rotation would eat the screen
this feature exists to show. The two zoom-triggering paths (manual button,
automatic orientation) intentionally behave differently on this one axis.

## Consequences

**Positive**

- Delivers the actual feature requested (big, legible, auto-oriented
  display) with no firmware/protocol changes — pure client-side JS/CSS,
  same as ADR 0045.
- Kept out of everyone's way by default: off, and the control itself
  invisible unless you're actually on an iPhone.

**Negative / open**

- **Not yet flashed or verified on real hardware or a real iPhone**, at the
  user's explicit instruction mid-session not to flash without asking
  first (a second concurrent session is using the same physical device for
  unrelated `launcher-adv-mirror` work). Everything here is verified by
  regenerating `WebAssets.h` and running `tools/test_dom_keyboard.cjs`
  (structure/wiring only — jsdom has no `orientation` media feature or real
  iPhone UA to exercise the actual auto-trigger path against) plus a local
  offline preview in Chrome (desktop UA, so the iPhone-gated toggle
  correctly stayed hidden — the one thing that preview *could* confirm).
- A manual zoom-button click while auto-zoom is on will get silently
  overridden back to the fill-to-landscape size on the next orientation
  change, since `applyAutoZoom()` doesn't try to detect or respect a manual
  override in between. Acceptable for now — rotation events are infrequent
  and user-driven — but worth knowing if it ever feels like the page is
  fighting the user.
- `min-width` breakpoints and this feature's "phone vs. not" logic (UA
  string vs. `pointer:coarse`) are two different signals for a similar
  question, for different reasons (one's about screen real estate, this
  one's about "does this specific device have AirPlay-to-car-stereo appeal
  and no hardware keyboard at all"). Not unified into one check on purpose —
  they're actually answering different questions even though they overlap
  on an iPhone specifically.

## Alternatives considered

- **Reuse the existing stepped zoom SELECT (2x-5x) instead of a new
  fill-to-viewport sizing function.** Rejected: the point is filling an
  unpredictable, actual landscape viewport (which varies by phone model and
  browser chrome), not picking the closest fixed multiplier — a fixed step
  would either waste space or overflow depending on the device.
- **Couple this to `setCapture()` like the manual zoom button.** Rejected
  explicitly — see Decision above; the driving/mounted use case is watching,
  not typing.
- **Detect "phone" via `matchMedia('(pointer:coarse)')` alone (the existing
  `isTouch`), instead of a separate iPhone-specific UA check.** Rejected:
  `isTouch` also matches iPads and Android phones, and the "party trick"
  this ADR describes (AirPlay to a car stereo) is specifically an iPhone
  workflow the user described — a UA-gated `isIphone` says that precisely,
  where `isTouch` would over-match.
