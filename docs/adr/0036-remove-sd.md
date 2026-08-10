# ADR 0036 — Remove the SD card feature entirely

## Context
The device has a microSD slot and the firmware grew a manager for it (ADR 0008):
mount/unmount, capacity reporting, FS detection, and an explicit FATFS format
path with a two-step browser confirm. It had a menu screen, four HTTP routes and
a dashboard panel.

The user asked for it gone from everything.

## Decision
Removed, not disabled. Deleted `src/sd_manager.{cpp,h}` (17,357 B of source),
the `Screen::Storage` menu screen and its `drawStorage()`, the four `/api/sd/*`
routes, and the dashboard panel with its CSS and script.

`SD.h` and the FATFS layer are no longer linked at all, which is where most of
the saving comes from: **flash 32.7% -> 31.2%**, about 48 kB. The mirror itself
is untouched.

ADR 0008 stays in the repo. It records why the format path used the FATFS layer
directly rather than Arduino's `format_if_empty` flag, and that reasoning is
still correct — it is simply no longer in use. A decision record is a log, not
a description of the present tree.

## What removal exposed
`drawHome()` built its subtitle with a chain ending in an open `else`:

    if (i == 0) { ... Network ... }
    else if (i == 1) { ... Storage ... }
    else if (i == 2) { ... System ... }
    else { ... Mirror, %d clients ... }      // <-- catches i == 3 AND i == 4

With five items the Key Test row was reached by that `else` and printed the
Mirror client count as its own subtitle. It had been wrong since the row was
added. Deleting Storage shifted every index down one and would have moved the
defect onto Mirror rather than fixing it, so the chain now terminates in an
explicit `i == 2` with a real `else` for Key Test.

This is the third defect in this project found by removing something rather
than by adding a test: the paint-order bug (ADR 0031) and the duplicate mic
(ADR 0035) were the others. All three were invisible while the covering
element was present.

## Consequences
- Home is four items. The URL lines under the list moved up one row
  (`kBodyY + 4/5 * (kCharH+2)`), or they would have floated below a list that
  no longer reaches them.
- `serverHandle()` stays on the mirror class. Its comment named sd_manager as
  the example consumer; it now names host firmware generally. The accessor is
  the seam that makes the mirror droppable into other builds, which is the
  next task — removing its only current caller does not make it dead.
- Assertions are NEGATIVE, matching the screws and the duplicate mic: every
  removed element id, the stylesheet rules, the `/api/sd/` fetches, and the
  `fmtB` byte formatter that only this panel used. A left-behind panel would
  poll a 404 and show "checking..." forever rather than fail visibly.
- `identify_firmware` in the MCP server still greps for `sdcard` in binaries.
  That is for identifying OTHER people's firmware — stock M5 builds print
  `Failed to mount SDCARD` — and is unrelated to this feature. Left alone.
- The top-edge comments in index.html still name the microSD slot. The slot is
  physically there; the mockup describes the hardware, not our feature set.
