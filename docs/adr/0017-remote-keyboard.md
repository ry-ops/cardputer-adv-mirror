# ADR 0017 — Remote keyboard: generate the layout, send coordinates

**Status:** accepted
**Context:** browser keyboard should show the real ADV layout and actually drive the device

## Two decisions, both about avoiding a second source of truth

### 1. The layout is GENERATED from the vendor header, not transcribed

The page already had a keyboard. It was hand-transcribed, decorative, and
carried HID usage codes (`0x2a`, `0x28`, `0x4F-0x52`) that had **no relationship
to anything the firmware reads**. The ADV keyboard is a 4x14 matrix scanned over
I2C by a TCA8418; M5Cardputer maps `(row, col)` through
`_key_value_map[row][col]` to a character. HID codes never enter the picture.

`tools/gen_keymap.py` parses `_key_value_map[4][14]` out of M5Cardputer's
`Keyboard.h` and emits `web/keymap.js`. It refuses to emit anything if it does
not parse exactly 56 keys, so a vendor layout change fails loudly at build time
instead of producing a subtly wrong keyboard.

The transcribed version had also placed the arrow cluster from memory. It was
wrong -- see the test note below.

### 2. The wire carries MATRIX COORDINATES, not characters

`{"t":"key","r":2,"c":11,"shift":false,"fn":true}`

The firmware feeds `(r,c)` through the *same* `_key_value_map` a physical press
uses. So a remote press and a real press converge immediately onto one code
path. The alternative -- sending characters -- would have created a second input
vocabulary that could disagree with the hardware, which is exactly the failure
this project has hit repeatedly (ADR 0014: matching the wrong SSID; ADR 0015:
displaying a configured value instead of an observed one).

## Injection is queued, never applied inline

The WebSocket handler runs on the **AsyncTCP task**. `menu::handleKey()` mutates
menu state and draws to the panel; `loop()` does the same every frame. Calling it
from the socket callback is a data race on menu state and a concurrent SPI
transaction on the display.

So `keyinject::post()` only enqueues onto a FreeRTOS queue (never blocks, depth
16) and `loop()` drains at most 8 per pass. Same reasoning that forced SD
formatting off the async task in ADR 0008.

Drain order in `loop()` is deliberate:

    keyinject::update();   // apply remote keys
    menu::update();        // repaint if that changed anything
    CardputerMirror.update();  // then transmit changed tiles

A remote press therefore appears in the very next frame, not the one after.

Queue depth 16 is small on purpose: a flooding browser should be refused at the
door rather than build a backlog that replays keys seconds after the user
stopped pressing. Refusals are counted in `keyinject::dropped()` rather than
silently discarded, because a nonzero count is a real symptom.

## Arrows: the ADV has none

There are no dedicated arrow keys. The menu reads `fn` + `;` `.` `,` `/`. The
browser translates real arrow keys into that chord automatically, so a laptop
arrow key drives the on-device menu. Left/right were previously unhandled by
`menu::handleKey`; they now mean out/in, the standard meaning in a hierarchical
menu, since a single-column menu has no horizontal axis.

## Testing: a C++ build cannot see JavaScript

`tools/test_keymap.mjs` runs the page's lookup tables headlessly under node and
checks every emitted coordinate against the vendor header. 14/14 pass.

**The first version of this test hardcoded the arrow coordinates from memory and
reported 3 failures against correct code.** The expectations were wrong, not the
implementation -- `,` `.` `/` sit at row 3 cols 10/11/12, not 11/12/13. The test
was rewritten to *derive* every expectation from `keymap.js` by scanning it, so
it cannot encode an assumption. This is the same lesson as ADR 0015: a check
that restates what you already believe cannot detect that you are wrong.

## Serving: one route, so inline

The firmware serves exactly one route (`/`). An external
`<script src="keymap.js">` would 404 on the device. `gen_web_assets.py`
substitutes the generated keymap into a `/*__KEYMAP__*/` placeholder and fails
loudly if the placeholder is missing.

Verified by decompressing the built `WebAssets.h` and confirming
`window.KEYMAP` is present, the placeholder is gone, and all 56 keys survived.

## Consequences

- `+3,460 B` flash, `+16 B` RAM.
- Page 12,806 -> 19,779 B raw, 7,439 B gzipped.
- Modifiers latch in the browser and are sent as *flags* alongside a key, which
  is how the hardware reports them (`KeysState.shift` / `.fn`).
- New dependency for tests only: node (env `jsvalidate`). Not needed to build.
