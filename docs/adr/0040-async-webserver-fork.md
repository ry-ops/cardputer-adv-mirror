# ADR 0040 — `esp32async/ESPAsyncWebServer` instead of the esphome fork

**Status:** Accepted — implemented. Verified: `env:cardputer-adv` builds
clean (no errors, no deprecation warnings after the `beginResponse_P` ->
`beginResponse` follow-up), existing test suite unaffected.
**Deciders:** firmware owner
**Related:** ADR 0038/0039 (the same "don't accidentally couple the core to
one host's toolchain" theme, one level down the dependency stack). First
surfaced by `launcher-adv-mirror`, whose build uses a newer arduino-esp32
core than this library's own `env:cardputer-adv`.

## Context

Building this library's `Mirror` into Launcher (`env:m5stack-cardputer`)
failed at `esphome/AsyncTCP-esphome@2.1.4` (a transitive dependency of
`esphome/ESPAsyncWebServer-esphome`, this library's WebSocket server):

```
.pio/libdeps/m5stack-cardputer/AsyncTCP-esphome/src/AsyncTCP.h:26:10:
fatal error: IPv6Address.h: No such file or directory
```

Checked directly, not assumed: `cardputer-adv-mirror` pins
`platform = espressif32@6.9.0`, an arduino-esp32 2.x-era core that ships a
standalone `cores/esp32/IPv6Address.h`. Launcher pins arduino-esp32 **3.3.9**
(`framework-arduinoespressif32@3.3.9`), whose `cores/esp32/` has no such file
— IPv6 support was folded directly into `IPAddress` in arduino-esp32 3.x.
`esphome/AsyncTCP-esphome` (checked its `main` branch too, not just the
published 2.1.4) unconditionally `#include`s `IPv6Address.h`, so it simply
cannot build against an arduino-esp32 3.x core, Launcher or otherwise. Not a
bug in this project — an unrelated third-party library not (yet) updated for
a newer major core version it wasn't written against.

This isn't Launcher-specific either: any future host on an arduino-esp32 3.x
core hits the identical wall.

## Decision

Switch to `esp32async/ESPAsyncWebServer` (the actively maintained
`ESP32Async` org fork — the modern continuation of the `me-no-dev` lineage).
Checked its `AsyncTCP.h` directly: it guards the include —

```cpp
#include "IPAddress.h"
#if __has_include(<IPv6Address.h>)
#include "IPv6Address.h"
#endif
```

— so it builds against both the 2.x-era split-header layout and 3.x's
merged one. Same header name (`ESPAsyncWebServer.h`), same class names
(`AsyncWebServer`, `AsyncWebSocket`, `AsyncWebServerRequest`, ...), so this
was a `lib_deps` swap with no API changes in `CardputerMirror.cpp` beyond
one deprecated call (`beginResponse_P` -> `beginResponse`, same signature).

## Consequences

**Positive**

- The core library now builds against both an arduino-esp32 2.x core (this
  repo's own `env:cardputer-adv`) and a 3.x one (Launcher), verified for the
  former; the latter is what motivated this in the first place.
- Actively maintained fork (pushed within the last month at time of
  writing), rather than one only used by ESPHome's own build matrix.

**Negative**

- One more dependency-provenance fact to keep in mind: this project no
  longer tracks whatever version ESPHome itself ships, if that ever mattered
  for some future compatibility reason. It didn't for anything this project
  actually uses (`AsyncWebServer`/`AsyncWebSocket`, nothing ESPHome-specific).

**Neutral**

- `AsyncTCP-esphome` is still a legitimate, working dependency for any host
  on an arduino-esp32 2.x core. This ADR doesn't claim it's broken in
  general, only that it can't build against 3.x.

## Alternatives considered

- **Patch `esphome/AsyncTCP-esphome` locally** (vendor a fixed copy, or a
  build-time patch) to guard its `IPv6Address.h` include the same way.
  Rejected: reinvents a fix the `ESP32Async` fork already ships and
  maintains; patching a third-party dependency in place is more to keep in
  sync long-term than depending on a fork that already solved it.
- **Pin `launcher-adv-mirror` to an older arduino-esp32 2.x core** to avoid
  the incompatibility instead of fixing the dependency. Rejected: that's
  Launcher's own upstream toolchain choice, not this project's to override,
  and would mean forking Launcher's build config for a reason that has
  nothing to do with this integration.
