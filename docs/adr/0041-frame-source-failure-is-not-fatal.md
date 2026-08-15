# ADR 0041 — A frame source failure doesn't take down remote control

**Status:** Accepted — implemented. Verified: `env:cardputer-adv` builds
clean; launcher-adv-mirror verification pending (this is what unblocks
shipping `cardputer.local` there before the display mirror itself works).
**Deciders:** firmware owner
**Related:** ADR 0038 (`IHostAdapter`, `IFrameSource`), ADR 0039
(`SpiReadbackFrameSource`, whose current launcher-adv-mirror failure is what
surfaced this). launcher-adv-mirror ADR 0002 (readback), which this doesn't
change the correctness of, only its blast radius when it fails.

## Context

`Mirror::begin()` (both the `Config`-only and adapter-driven overloads)
returned `false` immediately if `IFrameSource::begin()` failed — before ever
calling `_startServer()`. That meant a frame source failure took the entire
server down with it: no HTTP page, no WebSocket, no remote key/button
injection. But nothing about the server, the WS control channel, or `_sink`
(remote input) actually touches `_src` — only `update()`/`scanOneTile()` do,
once per `loop()` pass, well after `begin()` has already returned.

Concretely surfaced by launcher-adv-mirror: `SpiReadbackFrameSource::begin()`
fails there right now (a real, separate bug — arduino-esp32 3.x's own SPI
class bypasses the standard ESP-IDF bus-registration bookkeeping
`spi_bus_add_device()` depends on, so there's no bus for it to attach to;
still being worked). Under the old behavior, that one failure meant *nothing*
in this integration was reachable — not just the mirror image, but the
`cardputer.local` page and remote keyboard too, neither of which have
anything to do with GRAM readback.

## Decision

Track frame-source health separately (`Mirror::_srcOk`) from whether the
server starts. `begin()` always proceeds to `_startServer()` regardless of
whether `_src->begin()` succeeded; `update()`/`scanOneTile()` check `_srcOk`
and simply do nothing if it's false, rather than the frame source's own
per-call defensiveness being the only thing standing between a failed
`begin()` and undefined behavior.

`_selfTest` stays `-1` when `_srcOk` is false — already the established
"nothing to report" value (ADR 0038's own convention for "this source has
none"), so no new wire-protocol field was needed for the browser to see a
sensible state.

## Consequences

**Positive**

- "The display mirror doesn't work" and "remote control doesn't work" are no
  longer the same failure. A host can ship working remote keyboard/button
  control today even while its frame source integration is still being
  debugged — exactly the situation that motivated this.
- No new fatal-failure surface added: everything that already worked when
  the frame source succeeded is unchanged.

**Negative**

- A browser connecting to a host whose frame source failed sees a page that
  never updates its mirror image, with no explicit "why" surfaced in the
  wire protocol beyond `selftest: -1`. Someone debugging blind (no serial
  access) could mistake "no picture, but keys work" for a different kind of
  bug. Worth a real status field in the `hello` message if this turns out to
  matter in practice; not added now to keep this change small.

**Neutral**

- Doesn't touch or fix `SpiReadbackFrameSource`'s actual launcher-adv-mirror
  failure — that's still open. This ADR is about blast radius, not root
  cause.

## Alternatives considered

- **Add a third `IHostAdapter` method for "does this host want mirror-only,
  control-only, or both."** Rejected: over-engineered for what's actually a
  runtime *failure* state, not a deliberate configuration choice — `_srcOk`
  already answers the question adapters need answered, without a new
  interface surface.
- **Leave `begin()` fatal, and have adapters catch the failure and retry
  with a no-op frame source instead.** Rejected: pushes the same fix onto
  every adapter author instead of fixing it once in the core, which is
  exactly the kind of duplicated-per-host logic ADR 0001's whole
  architecture exists to avoid.
