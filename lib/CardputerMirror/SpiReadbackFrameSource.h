/*
 * SpiReadbackFrameSource — ADR 0039.
 *
 * The same GRAM-readback trick as ReadbackFrameSource (ADR 0002: CASET/RASET
 * to set a window, RAMRD 0x2E, clock dummy bits, read RGB888-on-the-wire),
 * issued directly over ESP-IDF's spi_master driver instead of M5GFX. For
 * hosts that don't link M5Unified/M5GFX -- Launcher's Arduino_GFX build is
 * the first case, but this is general: any host without M5GFX benefits.
 *
 * Read-only and draw-nothing by design: this class never writes a pixel, so
 * it never needs to know how the host draws. That means it also can't run
 * ReadbackFrameSource's boot self-test unassisted -- see checkPattern().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "CardputerMirror.h"
#include <driver/spi_master.h>

namespace cmirror {

struct SpiPanelConfig {
    int8_t pinMosi;
    int8_t pinSclk;
    int8_t pinDc;
    int8_t pinCs;
    spi_host_device_t host = SPI3_HOST;   // must already be spi_bus_initialize()'d
                                           // by the host's own display driver --
                                           // this class never initializes the bus,
                                           // only attaches a device to it.
    uint32_t readHz    = 16 * 1000 * 1000; // ADR 0002: 16 MHz read vs 40 MHz write
    uint8_t  dummyBits = 16;               // M5GFX Panel_ST7789.hpp: dummy_read_pixel
                                            // = 16 for this exact driver. Rounded up
                                            // to whole bytes below (see .cpp) as a
                                            // documented simplification -- see ADR 0039.
    uint8_t  spiMode   = 0;
    uint16_t colOffset = 52;               // ADR 0002 / M5GFX: offset_x=52, offset_y=40
    uint16_t rowOffset = 40;               // for board_M5CardputerADV, rotation=1.
};

class SpiReadbackFrameSource : public IFrameSource {
public:
    explicit SpiReadbackFrameSource(const SpiPanelConfig& cfg) : _cfg(cfg) {}
    ~SpiReadbackFrameSource() override;

    bool begin() override;
    bool fetchTile(int tileIndex, uint16_t* dst) override;

    // IFrameSource::selfTest() stays -1 (this source has none, honestly) --
    // this class has no draw path of its own, so it cannot run ADR 0002's
    // "draw a pattern, read it back" test unassisted. An adapter that CAN
    // draw (through the host's own API) should draw a known pattern, build
    // the RGB565 it expects, and call this instead:
    //
    //   percent match (0..100), or -1 on allocation failure. Same tolerance
    //   rule as ReadbackFrameSource::selfTest() (+-1 per 5-bit channel,
    //   +-2 on the 6-bit green channel) to absorb RGB888->565 rounding.
    int checkPattern(int x, int y, int w, int h, const uint16_t* expectedRgb565);

private:
    SpiPanelConfig       _cfg;
    spi_device_handle_t  _dev  = nullptr;
    uint8_t*             _raw  = nullptr;   // scratch: dummy + kTilePx * 3 B, rgb888

    void _dcCommand() const;   // DC low -- next byte(s) are a command
    void _dcData() const;      // DC high -- next byte(s) are data/read
    // keepCs: true if a read or another write immediately follows on the
    // SAME chip-select assertion (SPI_TRANS_CS_KEEP_ACTIVE) -- CASET/RASET/
    // RAMRD/read must stay one continuous CS-low window, same as
    // Panel_LCD::readRect()'s startWrite()...endWrite() scope in M5GFX.
    void _writeBytes(const uint8_t* data, size_t len, bool keepCs);
    void _setWindow(int x, int y, int w, int h);
    bool _readRectRaw(int x, int y, int w, int h, uint8_t* rgb888Out);
};

}  // namespace cmirror
