# ADR 0012 — Refuse to upload while the serial port is held

**Status:** accepted
**Context:** third upload failure from the same cause; this one erased the app

## The failure

    Flash will be erased from 0x00010000 to 0x0010ffff...
    Compressed 15104 bytes to 10430...
    Writing at 0x00000000... (100 %)

    A serial exception error occurred: device reports readiness to read but
    returned no data (device disconnected or multiple access on port?)

Then, on the immediate retry, it could not even connect:

    Connecting...
    A serial exception error occurred: ...

`lsof` named the holder:

    Python  38039  ryandahlberg  4u  CHR  /dev/cu.usbmodem1101

## Why this one is worse than the earlier two

Read the order of operations. esptool announced the erase of `0x10000-0x10ffff`
-- the **application partition** -- then wrote only the 15 KB bootloader region
at `0x0`, then died. The app was erased and its replacement was never written.

The device is therefore not running old firmware. It is running **no**
application firmware. That is why the second attempt could not connect: there is
no app to open a USB CDC serial port, so `/dev/cu.usbmodem1101` no longer has
anything behind it, even though the node still exists.

The two earlier instances of this failed *before* the erase and were harmless.
This one landed inside it.

## Decision

`tools/pio.sh` refuses to begin an upload while any `/dev/cu.usbmodem*` is held,
prints `lsof` output naming the process, and exits 2 before invoking PlatformIO.

Only `upload` is guarded. `run`, `-t clean`, and everything else are untouched.

    REFUSING TO UPLOAD: /dev/cu.usbmodem1101 is held by another process.
        Python  38039 ryandahlberg  4u  CHR  /dev/cu.usbmodem1101
      Free it with:  kill -9 38039

A refused upload costs seconds. A half-erased one costs a recovery cycle and,
if it happens while nobody is watching the log, looks like a dead board.

## Why a guard rather than a discipline

I already committed, after the second occurrence, to killing captures before
uploads. That commitment failed on the third occurrence. A rule that depends on
remembering is not a fix -- the check belongs in the tool that does the upload,
where it cannot be forgotten.

Note also that the sandbox agent **cannot** kill host processes (`kill -9`
returns `Operation not permitted`), so the agent cannot clean this up on the
user's behalf. All the more reason to refuse before damage rather than repair
after.

## Recovery from the erased state

Boot the ROM download loader, which lives in mask ROM and is unaffected by an
erased app partition:

1. hold **G0**
2. tap **RESET**
3. release **G0**
4. `pio run -t upload`

## Consequences

- Uploads fail fast and legibly instead of intermittently and destructively.
- The guard depends on `lsof`, present by default on macOS.
