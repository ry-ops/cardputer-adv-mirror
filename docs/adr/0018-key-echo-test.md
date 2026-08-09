# ADR 0018 — A key that does nothing is not a key that failed

**Status:** accepted
**Context:** "the enter key works, we have no way to test the other keys"

## The report was right, and the cause was not the keyboard

All 56 keys were being delivered correctly. `menu::handleKey()` acts on **8 of
them** (`; . , / w s a d`, plus enter/del/tab/fn) and silently returns for the
other 48. From the outside that is indistinguishable from a broken input path:

    press 'q'  ->  nothing happens  ->  "the q key doesn't work"
    press 'q'  ->  nothing happens  ->  "the menu has no use for q"

Both look identical. **This is a defect in the diagnostic, not the user.** The
same mistake as ADR 0014 (a scan verdict that could not distinguish "not found"
from "matched the wrong thing") and ADR 0015 (a display that rendered the same
whether the code worked or not).

## Fix: echo every key, whatever the menu does with it

New **Key Test** screen. `menu::recordKey()` is called for EVERY key --
physical and remote, including modifiers and including keys the menu ignores --
and logs:

    WEB r2 c11 ;    +fn        <- arrived over WebSocket
    KEY r0 c5  5               <- physical press

Both paths report **hardware matrix coordinates**, because physical presses go
through `M5Cardputer.Keyboard.keyList()`, which is the same `(col,row)` the
TCA8418 reported. A remote press and a real press therefore produce identical
rows -- so if the browser row is missing but the physical row appears, the fault
is provably in the network path and not the keymap.

Coverage is tracked as a 56-bit mask (`seen N/56`) and turns green at 56. That
is the literal answer to "are all the keys available".

On the Key Test screen every key is a *specimen*, not a command -- otherwise
pressing `.` to test it would navigate away from the screen under test. Only
`` ` ``/del (back) and enter (clear) keep their meaning there.

## Fix: a sweep, so testing 56 keys is one click

`Test all 56 keys` presses every position in sequence at **14/s**. Drain
capacity is ~1000/s (8 per loop pass, `delay(2)`), giving a **70x headroom**
against the depth-16 queue. That margin is deliberate and measured, not
guessed: a sweep that overflowed the queue would drop events, and a dropped
event looks exactly like a key that does not work -- reintroducing the very
ambiguity this ADR exists to remove.

`keyinject::dropped()` is displayed in red when nonzero, so a coverage gap can
be *attributed* rather than guessed at.

## Both ends count independently

The browser tracks what it sent (`coverage n/56`); the device tracks what it
received. Comparing the two numbers localises any fault:

| Browser | Device | Meaning |
|---|---|---|
| 56/56 | 56/56 | path works end to end |
| 56/56 | < 56 | loss between browser and device |
| < 56 | — | browser never sent it; UI bug |

A single number could not distinguish these.

## Left/right

Requested from the start; `handleKey` had no branch for them at all, so they
were consumed and discarded. Now wired: left = back, right = activate. On a
single-column menu there is no horizontal axis, so out/in is the meaning that
exists.

## Consequences

- `+2,296 B` flash, `+104 B` RAM (8-entry log + 56-bit mask).
- Page 19,779 -> 21,945 B raw, 8,237 B gzipped.
- Home gains a 5th entry, "Key Test".
- The keyboard already had all 56 keys before this change. What was missing was
  the ability to *demonstrate* it.
