# ADR 0025 — Test the page the device serves, not web/index.html

**Status:** Accepted — implemented
**Context:** the ADV keyboard had been verified by three text-level tests, a
matplotlib render, and source pattern-matching — but had **never been executed
in a DOM.** Adding that test found nothing wrong with the keyboard and two
things wrong with how I was verifying it.

## web/index.html is a template, not a page

It contains the literal placeholder `/*__KEYMAP__*/` and no keymap.
`tools/gen_web_assets.py` substitutes `web/keymap.js` at build time and gzips the
result into `lib/CardputerMirror/WebAssets.h`; the firmware serves only `/`, so
an external `<script src>` would 404 on the device.

Opened directly, the template renders a "keymap missing" notice into `#kb` — by
design, and the page handles it explicitly (`if(!window.KEYMAP){...}`). So a DOM
test pointed at `web/index.html` tests a page **no browser ever sees**, and every
count assertion legitimately reports 0.

`tools/test_dom_keyboard.cjs` therefore gunzips `WebAssets.h` and tests **that**.
It also asserts `kIndexHtmlGzLen` matches the parsed byte count, which catches a
half-regenerated header.

## Two harness defects, both of which produced false results

1. **A dead script looked like a broken keyboard.** The page's first statements
   are `getContext('2d',{willReadFrequently:true})` then an immediate property
   set. jsdom has no canvas backend: `getContext` returns `null`, the *entire*
   top-level script throws, and the keyboard build further down never runs. My
   first stub ignored the options argument. Seven assertions failed and none of
   them was about the keyboard.

2. **Vacuous passes.** On the empty DOM, "no cap prints a case-variant
   secondary" and "U+2334 absent" both reported **ok** — trivially true when
   there are no caps and no text. Two green lines that meant nothing. The test
   now gates every count assertion behind a build check and exits early rather
   than reporting downstream results.

That is the second form of the ADR 0024 defect: a check that cannot vary with
the condition it is supposed to detect. Worth stating as a rule — **an
element-counting assertion must be preceded by a non-empty guard**, or it is
decoration.

## Also corrected: I asserted invented names

I wrote the first test against `buildKb()` and a `disabled` attribute on
BtnRst. Neither exists. `buildKb` came from my own earlier session summaries —
the real build is a top-level `window.KEYMAP.forEach`, no named function — and
BtnRst is inert **by behaviour**: it has an `onclick` that explains itself,
deliberately carrying no `disabled` attribute, because a control that silently
does nothing misrepresents the hardware (ADR 0022). The class names
(`.key .krow .lgd .dome .pri .sec .spc`) were correct; the entry points were not.
Read the file, do not trust the summary.

## Result

16 assertions, all passing against the served page, covering the facts measured
in ADR 0022: 4x14 = 56 cells, two-part keys (legend bar + dome), exactly 21
genuine secondaries and no case-variant ones, one-unit space with a drawn mark,
uppercase letter legends, no wide key, BtnG0 wired to send, BtnRst wired to
explain. Skips with exit 0 when jsdom is absent so a fresh checkout still runs
the other three suites.

**Still unverified:** a real browser (jsdom does no layout, so the `--ku` fitter
and the legend-bar continuity are untested) and hardware. Flashing is blocked in
this sandbox — `/dev/cu.usbmodem1101` exists as `crw-rw-rw-` but `open()` returns
`EPERM`, and esptool reports it as "port doesn't exist", which is misleading.
Upload must be run from the user's own terminal.
