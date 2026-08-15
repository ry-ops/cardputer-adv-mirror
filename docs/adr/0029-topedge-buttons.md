# ADR 0029 — BtnG0 and BtnRst move onto the case top edge, unlabelled

**Status:** Accepted — implemented (position/width superseded by 0030)

## Context
The two top-face controls lived in a `#topedge` strip below the mockup, with a
"TOP EDGE" label, a pin-list caption (OFF/ON slide, microSD pins, IR:G44) and
text on each button. The request: move the buttons to the top of the ADV, just
the buttons, no other text.

## Decision

### Tabs protruding above the case, not inside it
The free band between the case top edge and the recessed deck is 3.2% of case
height -- 6.0px at a 320px viewport, 16.9px at the 820px maximum. That cannot
hold a pressable target. The buttons are absolutely positioned on a zero-height
bar at the case top and protrude upward, which also matches how they sit on the
physical top face. `#dev` takes a matching `margin-top` so they are not clipped.

### x-positions are ordering, not measurement
The reference photo is front-on, so the top face does not appear in it. BtnG0
sits left, BtnRst right, following the documented top-edge order (BtnG0, slide
switch, microSD, BtnRst, IR). These are NOT measured fractions and should not
be treated as such -- unlike every other number in ADR 0028.

### Hittability beats proportional fidelity
Faithfully scaled the tabs are 0.020 of case width: 5.8px tall at a 320px
viewport. That would make the only two hardware buttons on the page unpressable
on a phone. Both dimensions take a floor (`max(34px, ...)`, `max(22px, ...)`).
This is a deliberate departure from the measure-everything rule of ADR 0028.

### All text removed, but BtnRst still explains itself
The label, pin-list and button text are gone; the buttons carry `title` and
`aria-label` only, so they remain reachable by screen reader while the case
stays free of standing prose.

BtnRst remains inert -- it drives EN, which cuts power to the SoC, and firmware
cannot actuate it (`esp_restart()` is a warm reset, a different operation). A
control that silently does nothing is worse than one that says why, so the click
surfaces a note that fades after 1.8s. `#g0state` is now opacity-0 until a press
adds `.show`, so it satisfies "no other text" in the resting state without
turning BtnRst into a lie. G0 presses use the same transient note.

## Consequences
Test suite 47 -> 53 assertions, including that both buttons are unlabelled but
aria-named, that both sit on the case rather than in page flow, and that no
`.tlbl`/`.tinfo` prose survived.

The pin-list text (microSD pins, IR:G44) was deleted, not relocated. If that
reference is wanted it belongs in the hamburger menu (ADR 0027), not the case.
