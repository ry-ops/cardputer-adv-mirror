# ADR 0026 — Remove the keystroke log; fix two false claims in the note

**Status:** Accepted — implemented
**Context:** user: "can we remove the keystroke viewer. i don't need to see
every keystroke validation. id=klog"

## What went

`<pre id="klog">` and its CSS rule, plus the two lines in `noteSent` that
formatted and prepended each line. The counters (`ksent`, `kcov`) and the `.hit`
flash on the pressed cap stay — they answer "did it land?" without a running
transcript, and `kcov` is the only readout for the 56-key sweep.

Removing the log made three things dead, which were also removed rather than
left as decoration:

- `noteSent(r,c,label,shift,fn)` -> `noteSent(r,c)`. The last three parameters
  existed only to build the log line.
- `sendKey(k, forceShift, asArrow)` -> `sendKey(k, forceShift)`. `asArrow`
  chose the log *label* and never affected the wire event; its own comment said
  so. With no log there is nothing left for it to select.
- `const kk=window.KEYMAP[spec.r][spec.c]` in the hardware-capture handler,
  assigned and then never read.

The ADR 0019 reasoning that lived in the `asArrow` comment — arrow caps are
dedicated keys, sending `fn` would make a browser press differ from a physical
one — was **kept and rewritten in place**, not deleted with the parameter.

## Two claims in the note were false

Auditing the prose against the served page before trimming it:

- **"Every cap shows both legends"** — false. 21 of 56. The other 35 are
  modifiers, space, and the 26 letters, whose shifted value is the same glyph in
  the other case; printing a secondary there was the exact bug fixed in ADR 0022.
  The note was still describing the pre-fix page.
- **"scales to fit all 14 columns at any panel width"** — false below ~330px.
  `fitKeyboard` clamps the unit to `max(16, …)`, so 14 columns need 224px plus
  chrome; at a 240px viewport the board is 280px wide and overflows. The clamp
  is correct (a 12px key is not clickable) — the *claim* was wrong.

Both were true when written and went stale when the code changed. The note is
now 743 B instead of 1465 B and states only things the page still does.

## Rule

Prose in the UI is untested output. These two claims survived every test suite
because no test reads English. When a claim states a **count** or a **range**,
either assert it in a test or do not print it — `test_dom_keyboard.cjs` already
asserts the 21, so that number is now covered; "any panel width" was removed
rather than asserted, because the honest version is a number nobody needs.
