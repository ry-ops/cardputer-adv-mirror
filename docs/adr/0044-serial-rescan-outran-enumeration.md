# ADR 0044 — Two ways "is the device actually there?" gave a wrong answer during dev-loop USB checks

**Status:** Accepted — documented, not yet automated (see Alternatives)
**Context:** two separate incidents in the same debugging session where
`ls`/`ioreg`/`system_profiler` disagreed with the device's actual state --
once a shell-side timing artifact (Incident 1), once a genuinely wedged
device that even `system_profiler` missed (Incident 2).

## Incident 1: a rescan right after reconnect missed the device

Mid-session, after the user reconnected the Cardputer ADV's USB cable, four
consecutive rescans from this shell all agreed on the same (wrong) answer:

    ls /dev/tty.* /dev/cu.*        -> no usbmodem entry
    ioreg -p IOUSB -l -w0 | ...    -> no children under either XHCI root at all

That last one is the detail that made it look conclusive: not "wrong device,"
not "wrong port" -- *nothing* USB-level, not even a partial/unrecognized
enumeration. Two Mac ports were tried. No hub was in the path. The device's
own dashboard (`http://10.88.135.147/`) answered HTTP 200 the entire time, so
the board itself was never in question -- only whether this shell could see
it on USB.

The user then checked **System Information -> USB** directly (not through
this shell) and it showed the device cleanly:

    USB JTAG/serial debug unit
      Manufacturer: Espressif
      Serial Number: 80:45:6B:76:FA:68
      Link Speed: 12 Mb/s

A `ls -la /dev/cu.* /dev/tty.*` run immediately afterward found
`/dev/cu.usbmodem1101` with no changes on the hardware side in between.

## Why this is a different failure than the Stick S3 case, earlier in the same session

Worth being precise about, because the two look identical from the transcript
but aren't:

- **Stick S3:** `ls`, `ioreg`, *and* `system_profiler SPUSBDataType | grep`
  all agreed on zero enumeration, across a cable swap, two ports, a driver
  install, and a documented boot-mode button sequence. Every independent
  check pointed the same direction. That one was a real non-enumeration
  (most likely a dead-flat factory battery, per the M5Stack community
  threads on this exact symptom) -- there was no disagreement to explain.
- **Cardputer, this ADR:** `ls`/`ioreg` said nothing was there;
  `system_profiler`/System Report said otherwise, at effectively the same
  moment. Two views of the same OS state disagreed. That disagreement is
  the actual finding here -- not "USB is flaky," but "this shell's view of
  `/dev` and IOKit lagged the device tree System Report reads."

## Incident 2: an upload wedges the device between its two USB personalities, and `system_profiler` goes quiet too

Later the same session, flashing an unrelated dashboard change to the same
Cardputer failed identically four times in a row:

    Uploading stub...
    Running stub...
    Stub running...
    Changing baud rate to 1500000

    A fatal error occurred: No serial data received.

The failure point never moved. Lowering `upload_speed` from 1500000 to
460800 in `platformio.ini` and retrying produced the exact same error at the
exact same step -- ruling out "this baud rate is too fast for the cable/port"
as the cause, which was the working theory going in.

After the failures, the device was gone from **both** signals at once: the
dashboard (`http://10.88.135.147/`) stopped answering (unlike Incident 1,
where it stayed up the whole time), and `system_profiler SPUSBDataType`
returned nothing for it either. That combination -- not just USB, but the
app itself unreachable over WiFi -- is what distinguishes a genuinely wedged
device from Incident 1's shell-side blind spot.

The likely mechanism: the ESP32-S3's native USB has two distinct
personalities on the same physical peripheral -- the ROM's USB-Serial/JTAG
identity (`Espressif`, PID `0x1001`, seen throughout this session whenever
the device is in download mode) and the *running app's own* USB-CDC
(`ARDUINO_USB_CDC_ON_BOOT=1`). esptool's reset-into-bootloader sequence
switches the chip from one to the other, and this session caught it stuck
mid-switch for an extended window, answering as neither.

**Recovery:** the user physically unplugged and replugged the cable. The
very next upload attempt succeeded outright, at the original 1500000 baud
that had failed identically four times immediately prior -- confirming the
speed was never the variable that mattered.

**A live claim in this session that turned out wrong:** immediately after
recovery, with the freshly-flashed app confirmed running (dashboard
answering HTTP 200), it was asserted that `system_profiler` "just doesn't
show the USB-CDC device by that name while running the app" -- a plausible-
sounding guess, offered without checking. Checked immediately after: **`system_profiler
SPUSBDataType` returned completely empty output** -- exit 0, no error, not
even the controller entries a Mac normally reports with nothing attached --
from the same command that had reliably found this exact device earlier in
this same session. Not "present under a different name," as guessed live;
absent from the command's output entirely, while two independent signals
(a completed `esptool` upload and a live HTTP 200 from the device) proved it
was actually there. The guess should not have been offered without checking
first -- see Decision point 4.

## Decision

Treat an empty `ls /dev/cu.*` / `ioreg` result as inconclusive, not as proof
of "no device," whenever it follows a physical reconnect within the same
debugging session -- and cross-check before reporting a hardware conclusion
to the user:

1. Prefer `system_profiler SPUSBDataType` (or asking the user to read
   System Information -> USB directly) over trusting a single `ls`/`ioreg`
   pass as authoritative, specifically in the window right after a
   plug/unplug.
2. If a rescan comes up empty right after a reconnect, retry after a few
   seconds before concluding anything -- don't chain multiple *immediate*
   rescans and treat their agreement with each other as confirmation, since
   they can all be sampling the same stale window.
3. Don't tell the user "your hardware is fine, my shell has a blind spot"
   as a firm diagnosis (as happened live in this session) without having
   actually cross-checked via `system_profiler` first -- that framing
   should follow the check, not precede it.
4. `system_profiler` is a tiebreaker, not a ground truth -- Incident 2 caught
   it returning cleanly (exit 0) with **empty** output for a device that was
   simultaneously proven present by two independent signals (a completed
   upload, a live HTTP response). When it disagrees with a signal that
   strong, trust the strong signal, not the CLI check -- and don't offer a
   specific technical explanation for a discrepancy (e.g. "it's enumerating
   under a different name") without having actually looked. If genuinely
   stuck, the one source this session never saw be wrong is the user's own
   System Information.app -- ask them to check it directly rather than
   trusting another command run from this shell as final.
5. `esptool` failing with "No serial data received" immediately after
   "Changing baud rate," on a device with **native USB** (not an external
   USB-UART bridge like a CP2102/CH9102), is not primarily a baud-rate
   problem -- it's consistent with the reset wedging the chip between its
   ROM download-mode identity and the app's own USB-CDC. Retrying at a lower
   speed is not the fix (Incident 2 tried it, twice, with an identical
   failure); a physical unplug/replug to force clean re-enumeration is what
   actually resolved it.

## Consequences

**Positive**

- Distinguishes a real non-enumeration (Stick S3, outside this ADR: every
  tool agrees, over multiple independent attempts) from a shell-side timing
  artifact (Incident 1: tools disagree at one moment, agree moments later)
  from a genuinely wedged device (Incident 2: dashboard AND USB both go dark
  together) using actual decision rules instead of narrative feel.
- Cheaper for the user: no more "try a different port" / "check for a hub"
  round-trips for a condition that a single `system_profiler` cross-check,
  or the device's own dashboard, would have resolved immediately.
- Saves a future debugging pass from re-trying "lower the baud rate" against
  a "No serial data received" failure -- Incident 2 already spent two
  attempts on that dead end.

**Negative**

- `system_profiler SPUSBDataType` is noticeably slower than `ls`/`ioreg`
  (multiple seconds), so it shouldn't replace the fast check as the first
  move -- only as the tiebreaker once the fast check comes up empty right
  after a reconnect. Incident 2 also shows it isn't even a reliable
  tiebreaker every time, which weakens Decision point 1 somewhat -- it's
  still worth trying first, just not worth trusting blindly.
- No automated recovery exists yet for the wedged-mid-reset state in
  Incident 2. It currently requires a human to physically cycle power; there
  is no software-only recovery path from this shell once esptool's reset has
  left the chip answering as neither USB personality.

**Neutral**

- The root mechanism behind Incident 1 (IOKit device-tree publish lag vs.
  `/dev` node creation lag vs. something specific to how this shell issues
  the syscalls) is not nailed down here. Incident 2's mechanism (the two-USB-
  personality reset race) is a reasoned hypothesis consistent with the
  evidence, not something stepped through in a debugger. This ADR fixes the
  operating procedure around both symptoms; it doesn't claim to have found
  either kernel/silicon-level cause with certainty.

## Alternatives considered

- **Write a `tools/wait_for_serial.sh` poll-with-timeout helper now,**
  mirroring ADR 0012's `tools/pio.sh` guard. Deferred: ADR 0012 only became
  a tool after the *third* occurrence of its failure. Incident 1 was a first
  occurrence when this ADR was opened; Incident 2 is a second, different
  failure mode, not a repeat of the same one. If either recurs on its own,
  automate that one specifically rather than building one script for two
  unrelated symptoms now.
- **Add a `tools/pio.sh` guard that refuses/retries uploads after a
  "No serial data received" failure, forcing a wait-and-retry loop.**
  Deferred for the same reason: Incident 2 is n=1, and the only known fix
  (physical unplug/replug) isn't something software on this end can perform
  anyway -- a retry loop would just re-run the same failing handshake.
