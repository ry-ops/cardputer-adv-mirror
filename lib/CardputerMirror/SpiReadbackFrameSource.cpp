/*
 * SpiReadbackFrameSource — ADR 0039 implementation.
 * SPDX-License-Identifier: MIT
 */
#include "SpiReadbackFrameSource.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>

namespace cmirror {

// Standard MIPI DCS commands, same ones M5GFX issues internally
// (Panel_LCD.cpp) for this exact readRect() sequence.
static const uint8_t kCmdCaset = 0x2A;
static const uint8_t kCmdRaset = 0x2B;
static const uint8_t kCmdRamrd = 0x2E;

SpiReadbackFrameSource::~SpiReadbackFrameSource()
{
    if (_dev) spi_bus_remove_device(_dev);
    if (_raw) heap_caps_free(_raw);
}

bool SpiReadbackFrameSource::begin()
{
    pinMode(_cfg.pinDc, OUTPUT);
    digitalWrite(_cfg.pinDc, HIGH);

    spi_device_interface_config_t devcfg = {};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // IDF 5.x+ (Launcher's arduino-esp32 3.x core; NOT this library's own
    // env:cardputer-adv, still on an IDF 4.x-era core via espressif32@6.9.0)
    // added a clock_source field to this struct. Zero-initializing the
    // struct leaves it at 0 -- and per this enum's own doc comment ("enum
    // starts from 1, to save 0 for special purpose"), 0 is deliberately NOT
    // a valid clock source. Confirmed root cause of spi_bus_add_device
    // returning ESP_ERR_INVALID_STATE ("selected clock source is
    // unavailable") on IDF 5.x. The field doesn't exist at all pre-5.0, so
    // this must stay version-guarded rather than just always-set.
    devcfg.clock_source   = SPI_CLK_SRC_DEFAULT;
#endif
    devcfg.mode           = _cfg.spiMode;
    devcfg.clock_speed_hz = (int)_cfg.readHz;
    devcfg.spics_io_num   = _cfg.pinCs;
    devcfg.queue_size     = 1;
    // Half-duplex: reads clock data in over the SAME line writes go out on
    // (3-wire SIO, no MISO -- ADR 0002). dummy_bits stays 0 at the device
    // level on purpose: a nonzero device-level dummy_bits would insert a
    // dummy phase into EVERY transaction through this handle, including the
    // plain CASET/RASET/RAMRD command writes, corrupting their framing.
    // _readRectRaw() overrides it per-transaction with SPI_TRANS_VARIABLE_
    // DUMMY instead, only on the one transaction that needs it.
    devcfg.flags          = SPI_DEVICE_HALFDUPLEX;

    // Attaches to whatever bus the HOST already brought up with
    // spi_bus_initialize() -- this class never initializes the bus itself.
    // Fails here if that hasn't happened yet, which is a caller-ordering bug
    // worth surfacing loudly rather than silently reading garbage.
    esp_err_t err = spi_bus_add_device(_cfg.host, &devcfg, &_dev);
    _debugSpiErr = err;
    if (err != ESP_OK) {
        log_e("SpiReadbackFrameSource: spi_bus_add_device failed -- "
              "was the host's own display driver initialized first?");
        _dev = nullptr;
        return false;
    }

    const size_t rawBytes = kTilePx * 3;  // rgb888, 3 B/px on the wire (ADR 0002)
    _raw = (uint8_t*)heap_caps_malloc(rawBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_raw) _raw = (uint8_t*)heap_caps_malloc(rawBytes, MALLOC_CAP_8BIT);
    if (!_raw) {
        log_e("SpiReadbackFrameSource: alloc failed (need %u B)", (unsigned)rawBytes);
        return false;
    }
    return true;
}

void SpiReadbackFrameSource::_dcCommand() const { digitalWrite(_cfg.pinDc, LOW); }
void SpiReadbackFrameSource::_dcData()    const { digitalWrite(_cfg.pinDc, HIGH); }

void SpiReadbackFrameSource::_writeBytes(const uint8_t* data, size_t len, bool keepCs)
{
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    t.flags     = keepCs ? SPI_TRANS_CS_KEEP_ACTIVE : 0;
    spi_device_polling_transmit(_dev, &t);
}

void SpiReadbackFrameSource::_setWindow(int x, int y, int w, int h)
{
    // CASET/RASET stay under the SAME held CS as the RAMRD + read that
    // follows (see _readRectRaw) -- one continuous chip-select window from
    // the first CASET byte through the last read byte, matching
    // Panel_LCD::readRect()'s startWrite()...endWrite() scope in M5GFX.
    const int xs = x + _cfg.colOffset, xe = xs + w - 1;
    const int ys = y + _cfg.rowOffset, ye = ys + h - 1;

    const uint8_t caset[4] = {
        (uint8_t)(xs >> 8), (uint8_t)xs, (uint8_t)(xe >> 8), (uint8_t)xe
    };
    _dcCommand(); _writeBytes(&kCmdCaset, 1, true);
    _dcData();    _writeBytes(caset, 4, true);

    const uint8_t raset[4] = {
        (uint8_t)(ys >> 8), (uint8_t)ys, (uint8_t)(ye >> 8), (uint8_t)ye
    };
    _dcCommand(); _writeBytes(&kCmdRaset, 1, true);
    _dcData();    _writeBytes(raset, 4, true);
}

bool SpiReadbackFrameSource::_readRectRaw(int x, int y, int w, int h, uint8_t* rgb888Out)
{
    if (!_dev) return false;

    _setWindow(x, y, w, h);

    _dcCommand();
    _writeBytes(&kCmdRamrd, 1, /*keepCs=*/true);
    _dcData();

    // spi_transaction_ext_t + SPI_TRANS_VARIABLE_DUMMY: the real ESP-IDF
    // hardware dummy-bit phase (properly tri-stated on the shared data line
    // while the panel starts driving its response), not a hand-rolled
    // "send extra bytes" approximation. Last transaction in the CS window --
    // no CS_KEEP_ACTIVE, so CS releases normally when this completes.
    spi_transaction_ext_t t = {};
    t.base.flags     = SPI_TRANS_VARIABLE_DUMMY;
    t.base.length    = 0;
    t.base.rxlength  = (size_t)w * h * 3 * 8;
    t.base.rx_buffer = rgb888Out;
    t.dummy_bits     = _cfg.dummyBits;

    return spi_device_polling_transmit(_dev, (spi_transaction_t*)&t) == ESP_OK;
}

bool SpiReadbackFrameSource::fetchTile(int idx, uint16_t* dst)
{
    const int tx = (idx % kTileCols) * kTileW;
    const int ty = (idx / kTileCols) * kTileH;

    if (!_readRectRaw(tx, ty, kTileW, kTileH, _raw)) return false;

    // RAMRD returns 18-bit color as 3 bytes (6 meaningful bits/channel,
    // left-justified). Truncate down to RGB565 the same way ReadbackFrame
    // Source's rgb565_t conversion does. UNVERIFIED against real hardware
    // until checkPattern() runs -- see ADR 0039's stated risk.
    for (size_t i = 0; i < kTilePx; ++i) {
        const uint8_t r8 = _raw[i * 3 + 0];
        const uint8_t g8 = _raw[i * 3 + 1];
        const uint8_t b8 = _raw[i * 3 + 2];
        dst[i] = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));
    }
    return true;
}

int SpiReadbackFrameSource::checkPattern(int x, int y, int w, int h, const uint16_t* expectedRgb565)
{
    const size_t n = (size_t)w * h;
    uint8_t* buf = (uint8_t*)malloc(n * 3);
    if (!buf) return -1;
    if (!_readRectRaw(x, y, w, h, buf)) { free(buf); return -1; }

    int match = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t r8 = buf[i * 3 + 0], g8 = buf[i * 3 + 1], b8 = buf[i * 3 + 2];
        const uint16_t got  = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));
        const uint16_t want = expectedRgb565[i];
        // Same tolerance as ReadbackFrameSource::selfTest() -- absorbs
        // RGB888->565 rounding, not a real match slop.
        const int dr = ((want >> 11) & 0x1F) - ((got >> 11) & 0x1F);
        const int dg = ((want >>  5) & 0x3F) - ((got >>  5) & 0x3F);
        const int db = ( want        & 0x1F) - ( got        & 0x1F);
        if (dr >= -1 && dr <= 1 && dg >= -2 && dg <= 2 && db >= -1 && db <= 1) ++match;
    }
    free(buf);
    return (int)((match * 100) / n);
}

}  // namespace cmirror
