# ADR 0032 — Note text removed; menu and keyboard toggle confirmed shipping

**Status:** Accepted — implemented

## Context
The user reported not having seen the hamburger menu, asked whether the S3A
sticker work was ongoing, restated the requirement to switch between the
physical and on-screen keyboard, and asked for a block of explanatory page
text to be deleted.

Searching the archived transcript found the original request (move Full
refresh, Swap R/B, Invert, Save PNG, the stream readouts, Capture my keyboard
and Test all 56 keys into a menu) and the ADR that implemented it, 0027.

## Findings
The menu is present in the served page and has been since ADR 0027. Its
trigger is a `☰` button in the status bar BELOW the mockup, which is why it
did not appear in a screenshot cropped to the case. It contains every item
that was asked for.

`Capture my keyboard` inside that menu is the physical/on-screen switch. It
toggles `hwCapture`, which routes real keystrokes to the device; the `⌨
capturing keys` badge and its `stop` button sit OUTSIDE the menu on purpose,
because capture swallows the user's real keystrokes and its state must remain
visible once the menu closes.

The S3A sticker is deliberately finished, not in progress: it is a tinted
panel with a colour band, per ADR 0030. Reproducing the real artwork is
declined on both routes -- CSS would mean inventing pin numbers, and a photo
crop is unreadable below ~200px of case width. That is the standing decision.

## Decision
Remove the `.note` block and its now-orphaned CSS rule. The facts it carried
are preserved in ADR 0019 (dual-glyph caps, dedicated arrow keys reporting as
`; , . /`) and ADR 0001 (byte-order swap, GRAM readback reliability), and both
behaviours remain covered by assertions.

## Consequences
Nineteen assertions added: menu contents by id, the capture badge's placement
outside the menu, and three that the note text, its element and its CSS rule
are all gone. Total 97.

One of the new assertions initially failed -- `menu starts closed`. The code
was right; the assertion was wrong. An earlier block in the same suite drives
the menu open to prove Escape is yielded to the device during capture, and
leaves it open. A contents block must not re-assert open/close state that an
earlier block deliberately mutated.
