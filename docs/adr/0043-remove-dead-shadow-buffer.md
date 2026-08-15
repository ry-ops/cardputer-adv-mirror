# ADR 0043 — Remove the unused full-frame shadow buffer

**Status:** Accepted — implemented. Verified: `env:cardputer-adv` builds
clean; launcher-adv-mirror hardware verification pending (needs a physical
button press to get the device back into the home menu before it can be
reflashed and tested).
**Deciders:** firmware owner
**Related:** ADR 0042 (heap guard around `binaryAll()`), whose investigation
is what surfaced this. launcher-adv-mirror's `USE_CANVAS` work, whose
PSRAM-less hardware made the missing 64.8 KB actually matter.

## Context

`Mirror` allocated two buffers per instance: `_tile` (5,400 B, a per-tile
RGB565 scratch buffer genuinely used by `scanOneTile()`/`publishTile()`) and
`_shadow` (64,800 B, one `uint16_t` per screen pixel). `scanOneTile()` wrote
each changed tile's pixels into `_shadow` with a comment claiming this kept
"the shadow authoritative for future full-frame pushes."

No code anywhere in the library ever *read* `_shadow`. `forceFullFrame()`
(the only full-frame path that exists) just clears `_crc[]`/`_force[]` and
lets the normal tile scan re-fetch fresh pixels from `IFrameSource` on the
next pass — it doesn't touch `_shadow` at all. The buffer was pure dead
weight: a full screen's worth of RAM, allocated and kept updated on every
changed tile, for a "future" feature that was never built.

This went unnoticed because on PSRAM-equipped hosts 64.8 KB is background
noise. It stopped being background noise on launcher-adv-mirror's actual
ADV hardware, which ADR 0042's investigation confirmed has no PSRAM at all
(`maximum_ram_size: 327680` in the board definition — internal SRAM only).
Between that missing 64.8 KB, the `USE_CANVAS` framebuffer (another 64.8 KB),
Launcher's own baseline, and the WiFi/AsyncWebServer stack, internal SRAM
was tight enough that a ~40-byte allocation inside
`AsyncWebServerResponse::addHeader()` failed on the very first HTTP request
— see ADR 0042 for that crash's decode.

## Decision

Delete `_shadow` and `kShadowBytes` entirely. Change detection already runs
on `_crc[kNumTiles]` (12 × 4 B = 48 B) computed straight from the freshly
fetched `_tile` scratch buffer in `scanOneTile()` — that's the real,
already-working dirty-tile mechanism. Removing `_shadow` doesn't change
what's diffed against what; it only removes a buffer nothing consumed.

## Consequences

**Positive**

- Recovers 64,800 B of heap per `Mirror` instance — on a PSRAM-less host,
  this alone may be the difference between the mirror surviving normal
  `AsyncWebServer` request handling and aborting on the first HTTP request.
- No behavior change: `_shadow` was write-only, so nothing that ever read a
  mirror frame was reading from it.
- Simpler `scanOneTile()` — one less buffer to keep in sync.

**Negative**

- If a real "push a full frame in one shot" feature gets built later, it'll
  need its own buffer (or better, stream tiles instead of assembling one) —
  this ADR doesn't preclude that, it just stops paying for it unbuilt.

**Neutral**

- Doesn't address the *other* possible allocation-failure sites inside
  AsyncWebServer/AsyncTCP's normal request path (ADR 0042 only guards
  `publishTile()`'s own send). This buys back headroom; it doesn't prove
  every remaining `new` in that path is now safe on PSRAM-less hardware.

## Alternatives considered

- **Replace `_shadow` with a per-tile CRC array**, as originally proposed
  before writing this ADR. Turned out to be unnecessary — `_crc[kNumTiles]`
  already exists and already does exactly that job. There was nothing to
  build; only something to delete.
- **Shrink `_shadow` instead of removing it** (e.g., keep it but at lower
  color depth). Rejected once it was confirmed nothing reads it — a smaller
  unused buffer is still an unused buffer.
