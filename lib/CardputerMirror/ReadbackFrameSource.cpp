/*
 * ReadbackFrameSource — ADR 0002 implementation.
 *
 * Split out of CardputerMirror.cpp (ADR 0039's implementation) so that file
 * -- which contains Mirror itself -- no longer needs to #include <M5Unified.h>.
 * Mirror only ever touches a frame source through the abstract IFrameSource
 * interface; M5Unified was only ever needed by THIS class's method bodies.
 *
 * A host without M5Unified/M5GFX (Launcher's Arduino_GFX build, ADR 0039)
 * compiles this file to nothing -- __has_include guards it out entirely --
 * and simply never constructs a ReadbackFrameSource, using
 * SpiReadbackFrameSource instead. Nothing links a missing symbol, because
 * nothing references one.
 *
 * SPDX-License-Identifier: MIT
 */
#include "CardputerMirror.h"

#if __has_include(<M5Unified.h>)
#include <M5Unified.h>

namespace cmirror {

bool ReadbackFrameSource::begin()
{
    // M5GFX sets cfg.readable = true for board_M5CardputerADV, but MISO is not
    // wired (pin_miso = -1) so reads run half-duplex over MOSI (spi_3wire ->
    // SPI_SIO). Register reads demonstrably work (autodetect IDs the ST7789
    // that way); GRAM reads are the open risk, hence selfTest().
    return M5.Display.width() > 0;
}

bool ReadbackFrameSource::fetchTile(int idx, uint16_t* dst)
{
    const int tx = (idx % kTileCols) * kTileW;
    const int ty = (idx / kTileCols) * kTileH;
    // IMPORTANT: the uint16_t* overload of readRect() does NOT produce rgb565_t.
    // LGFXBase.cpp:1759 constructs its pixelcopy with swap565_t::depth, so the
    // result is byte-swapped relative to everything we write. Measured on this
    // unit: TFT_GREEN (0x07E0) read back as 0xE007 -> #E70039 in the browser.
    // Casting to rgb565_t* selects the template overload, which converts from
    // _read_depth (rgb888_3Byte) into true RGB565 with no byte swap.
    M5.Display.readRect(tx, ty, kTileW, kTileH, (lgfx::rgb565_t*)dst);
    return true;
}

int ReadbackFrameSource::selfTest()
{
    // Draw a known pattern into a corner, read it back, compare. This is the
    // only way to learn whether 3-wire GRAM readback is trustworthy here.
    const int W = 32, H = 16;
    static const uint16_t kPat[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

    M5.Display.startWrite();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            M5.Display.drawPixel(x, y, kPat[((x >> 2) + (y >> 2)) & 3]);
    M5.Display.endWrite();
    M5.Display.display();
    delay(20);

    uint16_t* buf = (uint16_t*)malloc((size_t)W * H * 2);
    if (!buf) return -1;
    // Same swap565_t trap as fetchTile() — must use the rgb565_t overload, or
    // this self-test measures LGFX's byte order rather than GRAM readback
    // fidelity. With the uint16_t* overload this scored exactly 25%: only
    // 0xFFFF (palindromic) survived the swap, while 0xF800/0x07E0/0x001F did
    // not. That 25% was a byte-order artifact, NOT unreliable readback.
    M5.Display.readRect(0, 0, W, H, (lgfx::rgb565_t*)buf);

    int match = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint16_t want = kPat[((x >> 2) + (y >> 2)) & 3];
            uint16_t got  = buf[y * W + x];
            // Tolerate RGB565 rounding from the 888 read path (+-1 per channel).
            int dr = ((want >> 11) & 0x1F) - ((got >> 11) & 0x1F);
            int dg = ((want >>  5) & 0x3F) - ((got >>  5) & 0x3F);
            int db = ( want        & 0x1F) - ( got        & 0x1F);
            if (dr >= -1 && dr <= 1 && dg >= -2 && dg <= 2 && db >= -1 && db <= 1)
                ++match;
        }
    }
    free(buf);
    return (match * 100) / (W * H);
}

}  // namespace cmirror

#endif  // __has_include(<M5Unified.h>)
