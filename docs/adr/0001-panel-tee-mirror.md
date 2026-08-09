# ADR 0001 — Panel tee mirror

**Status:** Proposed (build after 0002 validates the transport)
**Deciders:** firmware owner
**Related:** 0002 (shares layers 3-5), 0004 (extends this)

## Context

We want a pixel-exact browser mirror of the Cardputer ADV display at a usable
frame rate. M5GFX renders through an abstract `lgfx::Panel_Device`; every draw
operation on the ADV funnels into a `Panel_ST7789` instance created during
autodetect in `M5GFX.cpp`.

The full set of drawing entry points is small and fully virtual
(`M5GFX/src/lgfx/v1/Panel.hpp:111-144`):

```
setRotation, writeCommand, writeData, display, writeBlock, setWindow,
drawPixelPreclipped, writeFillRectPreclipped, writeImage, writeImageARGB,
writePixels, readRect, copyRect, writeFillRectAlphaPreclipped
```

Because these are pure virtual, a subclass can intercept all of them.

## Decision

Introduce `Panel_Mirror : public lgfx::Panel_ST7789`. It overrides the write
path, forwards each call to the base class (so the physical panel behaves
exactly as before), and *additionally* writes the same pixels into a shadow
RGB565 framebuffer, marking touched tiles dirty.

```cpp
class Panel_Mirror : public lgfx::Panel_ST7789 {
  void writeFillRectPreclipped(...) override {
    Panel_ST7789::writeFillRectPreclipped(...);   // real hardware
    _shadow.fillRect(...);                         // tee
    _markDirty(x, y, w, h);
  }
  // ...same shape for writeImage / writePixels / writeBlock / copyRect
};
```

Installed by replacing the autodetected panel after `M5.begin()`, reusing the
existing bus and config so no pin knowledge is duplicated.

It exposes `TeeFrameSource`, satisfying the same `IFrameSource` interface that
ADR 0002's `ReadbackFrameSource` implements. Layers 3-5 (dirty-tile scheduler,
wire protocol, browser UI) are inherited from 0002 unchanged.

## Consequences

**Positive**

- Pixel-exact by construction: the shadow sees the identical pixel stream the
  panel sees.
- No SPI read traffic at all. The 16 MHz read clock and the ~48.6 ms
  full-frame readback cost of ADR 0002 both disappear. Frame rate is bounded by
  WiFi and the dirty-rect area, not by the bus — 30 fps is realistic.
- Immune to the 3-wire-SIO readback risk that ADR 0002 carries.
- Dirty rectangles are *exact* (we know precisely what was drawn), so no CRC
  hashing pass is needed and no change is ever missed.
- Works even with the backlight off, and while the panel is asleep.

**Negative**

- Requires recompiling the firmware you want to mirror. For third-party
  firmware (Bruce, M5Launcher, nemo) that means maintaining a patch.
- `writeImageARGB` and the alpha-blended paths must be reimplemented in the
  shadow to stay pixel-exact; getting blending subtly wrong yields a mirror
  that drifts from the panel.
- Couples to LGFX internals. A refactor of the `Panel` virtual set upstream
  breaks the subclass. Pin to a known M5GFX version.
- +63.3 KiB SRAM for the shadow, same as 0002.

**Neutral**

- `readRect` can be served from the shadow instead of the panel, making
  application-level screen reads faster than stock as a side effect.

## Alternatives considered

- **Wrap `M5GFX` instead of `Panel_ST7789`.** Rejected: applications call
  hundreds of `LGFXBase` drawing methods; intercepting at the panel layer
  catches all of them at 14 override points instead.
- **Patch M5GFX in place** rather than subclassing. Rejected: makes the library
  un-updatable and pollutes every other project sharing the PlatformIO cache.
