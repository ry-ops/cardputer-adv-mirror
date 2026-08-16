# ADR 0047 — 1262 HAT mockup, drawn in CSS, and the top-edge buttons realigned to it

**Status:** Accepted — implemented, not yet flashed/verified on hardware
(see Consequences).
**Deciders:** firmware/dashboard owner
**Related:** ADR 0028 (device mockup), ADR 0029/0030 (top-edge buttons —
this ADR supersedes their measured positions), ADR 0033/0035 (stamp/label
photos — this ADR deliberately does NOT follow that precedent).

## Context

Proof-of-concept idea, paired with ADR 0046's landscape auto-zoom: a
wardriving loadout wants location (GNSS) and long-range radio (LoRa), and
M5Stack sells exactly that combination as a single "1262" HAT (SX1262 LoRa +
GNSS) for the Stick product line's 8-pin header mount. The user supplied two
reference images — a product photo of the real hat, and a cropped detail of
its GNSS/LoRa label bar — and asked for it to be added to the top of the
Cardputer ADV mockup **using CSS**, plus the top-edge buttons (BtnG0,
BtnRst) shortened to 1/3 of their current height and layered visually on
top of the new hat.

## Decision

**The hat is CSS art, not a measured photo — a deliberate exception to
ADR 0033/0035's precedent.** Those ADRs moved the stamp/label FROM
CSS-recreated art TO real photo crops specifically because a measured photo
is more accurate than hand-tuned CSS for a fixed, real, physical detail.
This case is different: there is no reference photo of a 1262 HAT actually
mounted on a Cardputer ADV to measure against — the pairing doesn't exist as
a real product — and the user asked for CSS specifically. So `#devhat` is
a plain gradient/border box with an inset two-tone label
(`#devhatlabel .gnss` / `.lora`), not an `<img>`.

**Positioning reuses `#topedge`'s existing coordinate space** (the
`position:absolute;height:0` box that BtnG0/BtnRst already anchor to via
`bottom:0`, established in ADR 0029) rather than inventing a second
"protrudes above the case" mechanism. `#devhat` sits behind the two buttons
in paint order (`z-index:2` vs. the buttons' `z-index:3`, added explicitly
rather than relying on DOM-order/auto-z-index behavior), so the buttons read
as mounted ON the hat.

**Centering is derived, not eyeballed twice.** `#devhat` is itself centered
on `#dev` (its `left`/width are symmetric — 2.2% and 97.8% from each edge),
so centering `#devhatlabel` *within* `#devhat` via `justify-content:center`,
combined with an exact 50/50 `flex` split between the GNSS and LoRa halves,
is what puts the blue/purple color break exactly on the device's own
horizontal centerline — confirmed by reading real `getBoundingClientRect()`
coordinates in a live preview (hat center and label center both landed at
the identical pixel), not assumed from the CSS alone. The label is sized to
58% of the hat's width specifically so grey plastic shows on both sides,
matching the reference photo, rather than the label running edge-to-edge.

**BtnG0/BtnRst repositioned to align with the ADV/S3A stickers, not the
top-face photo.** At the user's explicit correction, `#topedge #btnrst`'s
left edge now equals `#devlabel`'s right edge (1.880% + 21.034% =
22.914%), and `#topedge #btng0`'s left edge equals `#devsticker`'s left
edge (68.39%) — verified the same way, via live `getBoundingClientRect()`
comparison rather than trusting the arithmetic alone. ADR 0030's original
measured values (71.1% / 15.3%, from the real top-face photo) are kept in a
comment as history, explicitly marked superseded, not deleted — consistent
with how this project handles every other superseded ADR.

**Button height cut to 1/3, flagged rather than silently capped.** The
existing floor comment on `.tbtn` already explains why these buttons have a
44px-adjacent minimum (touch hittability); cutting to 1/3 (about 7.3px)
goes well under that floor and under ADR 0045's general 44px tap-target
rule. Implemented as asked — explicitly framed by the user as a starting
point for iteration, not a final call — but called out in the CSS comment
directly, not just in conversation, so a future reader (including a future
session) sees the tradeoff at the point where it was made.

**`#dev`'s top margin now clears the hat, not the buttons.** The margin
existed solely to keep whatever protrudes above the case from clipping
against page content above it; since the hat (`max(20px, cw*0.030)`) is now
taller than the shrunk buttons (`max(7.3px, cw*0.00667)`), the margin
formula moved to match the hat instead.

## Consequences

**Positive**

- Delivers a specific, explicit visual request (centered label, button
  realignment) verified against real computed geometry, not just "looks
  about right" from a screenshot.
- Kept fully separate from ADR 0046's feature (different toggle, different
  code path) despite landing in the same session — no shared state, no
  coupling introduced between "is the hat shown" and "is landscape
  auto-zoom on" (the hat has no visibility toggle at all; it's always
  drawn).

**Negative / open**

- **Not yet flashed to hardware**, same reason as ADR 0046 — the user
  caught an implicit flash mid-session and asked for explicit confirmation
  before any future upload, given a second concurrent session may be using
  the same physical device. Verified instead via a local, offline preview
  (the served page decompressed from `WebAssets.h` and served over a
  throwaway local HTTP server, since this Chrome automation session
  couldn't load `file://` URLs) plus `tools/test_dom_keyboard.cjs`.
- The 1/3-height buttons are, by the project's own established standard
  elsewhere (ADR 0045), too small to comfortably tap on a touchscreen. This
  is known and explicit, not an oversight — see Decision above — but it's a
  real regression in that one dimension until/unless revisited.
- No `IFrameSource`/firmware awareness of a real 1262 HAT exists or is
  implied by this change. This is purely a browser-side visual mockup; if a
  real hat is ever physically added to real hardware, none of its actual
  I/O (LoRa radio, GNSS fix) is wired to anything here.

## Alternatives considered

- **A real photo crop of the hat, `<img>`-based, matching ADR 0033/0035's
  established pattern.** Rejected per the user's explicit "using css"
  instruction, and because (unlike the stamp/label) there's no real
  hat-mounted-on-ADV photo to crop from in the first place — CSS isn't a
  fallback here, it's the only option that doesn't require inventing a
  composite photo.
- **Leave BtnG0/BtnRst at their ADR 0030 photo-measured positions and let
  the hat overlap wherever it lands.** Rejected at the user's explicit
  correction after seeing the first pass — sticker-aligned positioning was
  a deliberate, specific ask, not a suggestion.
- **Silently raise the 1/3 button height back toward something tappable.**
  Rejected: the user asked for a specific number as an explicit starting
  point ("let's start with 1/3"), which reads as an invitation to iterate
  together, not an implicit "but not literally." Flagging the concession in
  a comment was judged better than overriding the instruction unasked.
