# ADR 0004 — Mirror + full remote control

**Status:** Proposed (build after 0001) — goal achieved via a different path: 0002 + 0017 + 0037 shipped remote control on `ReadbackFrameSource`, not on 0001's `TeeFrameSource` as specified below. 0001 itself remains unbuilt.
**Deciders:** firmware owner
**Related:** 0001 (provides layers 2-5), 0002 (provides layers 3-5)

## Context

A mirror is read-only. The goal state is a remote desktop for the device:
see the screen *and* drive it — type, navigate, adjust backlight, grab
screenshots, browse the SD card — from a browser, with the device on a bench or
in a pocket.

The essential question was whether input injection requires patching
`M5Cardputer`. It does not. `Keyboard_Class` exposes a public seam
(`Keyboard.h:152-154`):

```cpp
void begin();
void begin(std::unique_ptr<KeyboardReader> reader);   // <-- injection point
uint8_t getKey(Point2D_t keyCoor);
```

`KeyboardReader` is an abstract base whose entire contract is
`begin()`, `update()`, and a protected `std::vector<Point2D_t> _key_list`
(`KeyboardReader.h`). Because the ADV's TCA8418 reader already remaps hardware
(row, col) into the original Cardputer's coordinate space, a synthetic reader
that appends `Point2D_t` values produces keystrokes indistinguishable from
physical ones — every downstream consumer (`keysState()`, `getKey()`,
`isPressed()`) works unmodified.

## Decision

Compose three parts:

1. **Video** — ADR 0001's `TeeFrameSource` (not 0002's readback: control
   demands low latency and no bus contention).
2. **Input** — a `RemoteKeyboardReader : public KeyboardReader` that merges
   physical TCA8418 events with events arriving over WebSocket, so local and
   remote input work simultaneously rather than one locking out the other.
3. **Control channel** — JSON commands over the existing WebSocket:
   backlight/brightness, power and reset, PNG screenshot capture, SD card
   listing / upload / download, and log streaming.

Installed as:

```cpp
auto reader = std::make_unique<RemoteKeyboardReader>(/*passthrough=*/true);
RemoteInput.attach(reader.get());
M5Cardputer.Keyboard.begin(std::move(reader));
```

## Consequences

**Positive**

- Full remote operation with **no patches to M5Cardputer or M5GFX** — both
  extension points (panel subclass, keyboard reader) are public and intended.
- Physical and remote input coexist.
- Reuses every layer built for 0001/0002; the genuinely new work is the reader
  and the command handlers.
- Enables headless CI: script a key sequence, capture a screenshot, diff it.

**Negative**

- **Security.** This is remote code execution by another name — anything the
  keyboard can do, the network can now do. Unauthenticated, it is a backdoor on
  the WiFi segment. Mandatory: a shared-secret handshake, binding to a chosen
  interface, and a physical opt-in (a key chord at boot) before control is
  armed. This is the single largest cost in this ADR and must not be deferred.
- Largest flash and RAM footprint of the four options, on a part with 8 MB flash
  and no guaranteed PSRAM. SD browsing and PNG encoding both want buffers.
- Screenshot PNG encoding costs CPU and RAM; expect ~100 ms and a transient
  allocation.
- Injected input can desynchronize firmware that reads the TCA8418 directly
  rather than through `Keyboard_Class`. Third-party firmware may do this.

**Neutral**

- The passthrough flag makes "remote-only" (kiosk/demo) mode a one-line change.

## Alternatives considered

- **USB HID injection from the host.** Requires the device to enumerate as a
  host and a physical cable, defeating the wireless goal.
- **Replacing the physical reader instead of merging.** Simpler, but the device
  becomes unusable by hand while connected. Rejected.
- **BLE HID keyboard.** Works for keys, but provides no video path, so a second
  transport would still be needed.
