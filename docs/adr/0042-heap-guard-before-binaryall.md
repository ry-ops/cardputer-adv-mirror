# ADR 0042 — Check heap headroom before `binaryAll()`, don't let it abort the device

**Status:** Accepted — implemented. Verified: builds clean; launcher-adv-mirror
hardware verification pending.
**Deciders:** firmware owner
**Related:** ADR 0038 (`IHostAdapter`), ADR 0041 (frame source failure isn't
fatal — same graceful-degradation stance, applied to a different failure
mode). launcher-adv-mirror's `USE_CANVAS`/`LauncherCanvasFrameSource` work,
whose first real test surfaced this.

## Context

`Mirror::publishTile()` calls `AsyncWebSocket::binaryAll(const char*, size_t)`,
which internally does `makeSharedBuffer()` — `std::make_shared<std::vector<
uint8_t>>(message, message + len)`, a heap allocation of up to `4 + 3 +
kTilePx*2` (~5.4 KB) copied out of the caller's stack buffer. This firmware
builds with `-fno-exceptions`. Under libstdc++, a `throw` that would normally
raise `std::bad_alloc` on allocation failure instead calls
`std::terminate()` when exceptions are disabled — so a single failed `new`
inside ESPAsyncWebServer's own send path aborts and reboots the whole device,
not just that one tile.

Surfaced by launcher-adv-mirror's `USE_CANVAS` frame source: on real hardware
(an ADV unit, quite possibly with no PSRAM — `CardputerMirror.cpp` already
carries an `allocPreferPsram()` fallback for exactly that), a client
connecting and the first changed tile hitting `binaryAll()` aborted with a
backtrace through `operator new` → `__cxa_throw` → `std::terminate()` →
`abort()`, decoded via `addr2line` against a debug build. The device was
otherwise idle (`clients=0` in the last heartbeat before the crash) — the
browser tab from earlier testing had simply reconnected on its own, which is
exactly the kind of client presence a heartbeat sampled every 3 s can miss.

## Decision

Before calling `binaryAll()`, check
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` against the size the copy
needs, plus a fixed safety margin for the vector/shared_ptr control block and
general fragmentation slop. If it doesn't fit, skip the send for this tile —
the next `update()` pass will retry once the tile's CRC still differs (it's
not marked clean), so a skipped tile is a delayed frame, not a lost one.

## Consequences

**Positive**

- A memory-pressure moment now costs one stale tile, not a reboot. Matches
  ADR 0041's stance: a failure in the mirror's own data path degrades the
  mirror, it doesn't take the whole device down with it.
- No change to the wire protocol or `IFrameSource`/`IInputSink` contracts —
  this is entirely internal to `publishTile()`.

**Negative**

- `heap_caps_get_largest_free_block()` is itself a walk of the heap's free
  list — not free, though cheap next to a WS send. Called once per changed
  tile, not per pixel, so this hasn't been a concern in practice.
- Papers over the actual scarcity rather than fixing it. If a host is tight
  enough on internal SRAM that this guard trips routinely, the mirror image
  will visibly lag or stall — the right fix there is reducing concurrent
  buffer footprint (canvas framebuffer, mirror shadow/tile buffers), not this
  guard, which only stops the specific failure mode from being fatal.

## Alternatives considered

- **Wrap the call in try/catch.** Rejected: exceptions are disabled
  firmware-wide (`-fno-exceptions`); enabling them just for this call site
  isn't how the flag works, and re-enabling exceptions project-wide is a much
  bigger change than this bug warrants.
- **Reduce `kTilePx` so the worst-case allocation is smaller.** Doesn't fix
  the underlying problem — any allocation can fail under fragmentation, and a
  smaller tile just moves the threshold, it doesn't remove the unguarded
  `new`. The heap check is the actual fix; tile sizing is a separate,
  unrelated tuning knob.
