# ADR 0038 — Adapter-driven `begin()`: `IInputSink` / `IHostAdapter`

**Status:** Proposed
**Deciders:** firmware owner
**Related:** generalizes 0001/0002 (`IFrameSource` is the existing half of this
seam) and 0004/0017 (input injection, already shaped this way in practice via
`onKey`/`onBtn`). First consumer: `launcher-adv-mirror` ADR 0001 (Launcher
fork), which needs this to exist without editing this repo's source.

## Context

This library states its own boundary in the README: *"the library owns no
policy of its own."* For the frame source that's already true —
`IFrameSource` (0001/0002) is injectable in principle. It isn't in practice:
`Mirror::begin(cfg)` hardcodes it —

```cpp
// CardputerMirror.cpp:109
static ReadbackFrameSource src;
_src = &src;
```

— so today there is exactly one frame source, chosen by this file, for every
host. Input has the same gap from the other direction: `onKey`/`onBtn`
(`CardputerMirror.h`) are raw C function pointers a host registers, already
carrying the right contract — *"Called from the AsyncTCP task ... MUST be
non-blocking and must not touch the display -- enqueue only"* — but nothing
bundles frame source + input sink + bus-sharing policy into one object a host
hands over at `begin()`. `src/keyinject.cpp` in this repo is, structurally,
already what a `Standalone` adapter's input sink would be — a queue drained
from `loop()` — it's just not behind a named interface.

A second host (Launcher, see `launcher-adv-mirror`) needs its own frame
source wiring and its own input path, supplied from *its* repo, without
touching this one. That requires an actual injection point at `begin()`, not
just an internal `IFrameSource` abstraction nothing outside this file can
reach.

## Decision

Add two headers next to the existing `IFrameSource.h`:

```cpp
// Core-owned interfaces. Firmwares implement them; the core only consumes them.

struct RemoteKey {
    uint8_t row, col;   // same (row, col) vocabulary keyinject.h already uses --
    bool    shift, fn;  // the wire protocol is not changing, only who receives it
};

class IInputSink {
public:
    virtual ~IInputSink() = default;
    virtual void begin() = 0;
    // Called from the AsyncTCP task -- MUST be non-blocking, MUST NOT touch
    // the display. Same contract onKey/onBtn already document today.
    virtual void inject(const RemoteKey& k) = 0;
    virtual void injectBtn(uint8_t btn, uint16_t ms) {}   // optional: BtnG0 etc.
    virtual void poll() {}                                 // optional: merge live HW input
};

// Opaque SPI-bus mutex. Adapters sharing the panel bus with the host's own
// writes return the host's real mutex; adapters with no such contention
// (see launcher-adv-mirror ADR 0004 for a worked example) return nullptr.
// The core takes/gives it only around IFrameSource calls, and only if non-null.
using PortMutex = void;

class IHostAdapter {
public:
    virtual ~IHostAdapter() = default;
    virtual void          begin()       = 0;   // install hooks, start sources
    virtual IFrameSource& frameSource() = 0;
    virtual IInputSink&   inputSink()   = 0;
    virtual PortMutex*    busLock()     = 0;
};
```

`Mirror` gains an overload:

```cpp
bool Mirror::begin(IHostAdapter& adapter);
```

which calls `adapter.begin()`, points `_src` at `adapter.frameSource()`, and
wires the AsyncTCP key/button handlers to `adapter.inputSink().inject()` /
`injectBtn()` — the same call sites `onKey`/`onBtn` use today, just reached
through the adapter instead of a loose function pointer.

**`Mirror::begin()` and `Mirror::begin(const Config&)` are unchanged.** They
keep constructing `ReadbackFrameSource` internally exactly as they do today,
so `src/main.cpp` in this repo needs zero changes. The new overload is purely
additive.

## Consequences

**Positive**

- A second host supplies its own frame source and input path from its own
  repo, with no edits to this library — the thing the README already claims,
  now actually reachable from outside this file.
- `src/keyinject.cpp`'s existing queue-and-drain shape is validated as the
  right pattern for any `IInputSink`, not rewritten.
- Zero behavior change for existing callers (`Mirror::begin()`,
  `Mirror::begin(cfg)`); this is a pure addition.

**Negative**

- One more virtual-dispatch layer between the AsyncTCP callback and the
  actual key application, on a path that is already documented as
  latency-sensitive.
- `PortMutex` as `void*` pushes the take/give correctness onto each adapter
  author; a wrong or missing `busLock()` produces exactly the SPI corruption
  ADR 0002 already identified as the risk of unserialized bus access.

**Neutral**

- `RemoteKey` formalizes the (row, col, shift, fn) tuple `keyinject.h` already
  uses informally; no wire-format change.

## Alternatives considered

- **`std::function` instead of virtual interfaces.** Rejected: pulls
  `<functional>` and its allocation overhead onto an 8 MB part for no
  behavioral gain over a vtable the compiler already emits for `IFrameSource`.
- **Templated `Mirror<Adapter>` instead of a virtual `IHostAdapter`.**
  Rejected: forces the adapter type into every translation unit that touches
  `Mirror`, and this library's own precedent (`IFrameSource`) is already
  virtual — consistency over a marginal vtable-call saving.
- **Fold `IInputSink` into `IHostAdapter` directly (no separate interface).**
  Rejected: a host with only a frame source and no meaningful input path
  (e.g. a pure logging/telemetry adapter) would be forced to implement a
  no-op sink instead of the core treating "no sink" as valid.
