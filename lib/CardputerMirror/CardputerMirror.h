/*
 * CardputerMirror — browser display mirror for M5Stack Cardputer ADV
 * ADR 0002: non-invasive GRAM readback.
 *
 * Verified hardware facts (M5GFX/M5Cardputer sources, not datasheets):
 *   board_M5CardputerADV = 24; Panel_ST7789 135x240, rotation 1 -> 240x135
 *   SPI3_HOST write 40MHz / read 16MHz; MISO NOT wired, spi_3wire=true (SIO)
 *   cfg.readable = true; _read_depth = rgb888_3Byte (3 B/px on the wire)
 *   Full frame = 97,200 B = 48.6 ms @16MHz -> ~20.6 fps hard ceiling
 *
 * Integration (two lines):
 *     CardputerMirror.begin();    // setup()
 *     CardputerMirror.update();   // loop()
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "IInputSink.h"

namespace cmirror {

// ---- Geometry. Divides exactly: 240/4 = 60, 135/3 = 45. ----
static constexpr int kScreenW  = 240;
static constexpr int kScreenH  = 135;
static constexpr int kTileCols = 4;
static constexpr int kTileRows = 3;
static constexpr int kTileW    = kScreenW / kTileCols;  // 60
static constexpr int kTileH    = kScreenH / kTileRows;  // 45
static constexpr int kNumTiles = kTileCols * kTileRows; // 12
static constexpr size_t kTilePx = (size_t)kTileW * kTileH;          // 2700

/*
 * Frame acquisition interface.
 *
 * THIS is the seam that makes ADR 0002 -> 0001 -> 0004 incremental.
 * Only this class differs between options:
 *   ReadbackFrameSource (ADR 0002, here) — polls panel GRAM
 *   TeeFrameSource      (ADR 0001)       — Panel_ST7789 subclass tees writes
 * The dirty-tile scheduler, wire protocol, codec and browser UI above this
 * interface are shared verbatim.
 */
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual bool begin() = 0;
    // Fetch one tile as RGB565 into dst (kTilePx entries). true on success.
    virtual bool fetchTile(int tileIndex, uint16_t* dst) = 0;
    // True if this source knows which tiles changed without reading them.
    virtual bool hasExactDirty() const { return false; }
    // Percent match (0..100) from a boot self-test, or -1 if this source has
    // none. ReadbackFrameSource overrides this with ADR 0002's pattern-based
    // test; a tee source has nothing analogous to test and keeps the default.
    virtual int  selfTest() { return -1; }
};

// ADR 0002 frame source: M5.Display.readRect() over 3-wire SIO.
class ReadbackFrameSource : public IFrameSource {
public:
    bool begin() override;
    bool fetchTile(int tileIndex, uint16_t* dst) override;
    // Draw a known pattern, read it back, return percent match (0..100).
    // Run before trusting any frame — 3-wire GRAM readback is the one risk
    // in ADR 0002 that source-reading cannot settle.
    int  selfTest() override;
};

// Opaque SPI-bus mutex (a FreeRTOS SemaphoreHandle_t, cast to PortMutex*).
// Adapters sharing the panel bus with the host's own writes return the
// host's real mutex; adapters with no such contention return nullptr. The
// core takes/gives it only around IFrameSource calls, and only if non-null.
// See ADR 0038, and launcher-adv-mirror ADR 0004 for a worked example of
// deciding which applies.
using PortMutex = void;

// Core-owned interface — ADR 0038. Firmwares implement it; the core only
// consumes it. The library itself never contains a concrete IHostAdapter.
class IHostAdapter {
public:
    virtual ~IHostAdapter() = default;
    virtual void          begin()       = 0;   // install hooks, start sources
    virtual IFrameSource& frameSource() = 0;
    virtual IInputSink&   inputSink()   = 0;
    virtual PortMutex*    busLock()     = 0;
};

struct Config {
    const char* ssid       = nullptr;   // nullptr -> start SoftAP
    const char* password   = nullptr;
    const char* apSsid     = "CardputerADV";
    const char* apPassword = "cardputer";
    uint16_t    port       = 80;
    // Per-update() SPI read budget. 4500us ~= 1 tile (4.05ms). Raise for
    // faster mirroring, lower to steal less time from the application.
    uint32_t    budgetUs   = 4500;
    bool        swapRB     = false;     // runtime-togglable from the browser
    bool        invert     = false;
    // When false, begin() leaves the radio entirely alone and assumes the
    // caller already brought WiFi up (see src/wifi_manager.h). Set false
    // whenever an external manager owns connection policy, or the two will
    // fight: this class would tear down a working STA link and start a SoftAP.
    bool        manageWifi = true;
};

class Mirror {
public:
    bool begin();
    bool begin(const Config& cfg);
    // Adapter-driven begin() — ADR 0038. Frame source, input sink, and SPI
    // bus lock all come from the adapter instead of being hardcoded here.
    // begin()/begin(cfg) above are unchanged and keep building a
    // ReadbackFrameSource internally; these overloads are purely additive.
    bool begin(IHostAdapter& adapter);
    bool begin(const Config& cfg, IHostAdapter& adapter);
    // Call from loop(). Reads at most cfg.budgetUs of tiles, pushes changes.
    void update();

    void   setBudgetUs(uint32_t us) { _cfg.budgetUs = us; }
    void   setSwapRB(bool v)        { _cfg.swapRB = v; forceFullFrame(); }
    void   setInvert(bool v)        { _cfg.invert = v; forceFullFrame(); }
    void   forceFullFrame();
    String ipAddress() const;
    int    clientCount() const;
    int    selfTestScore() const { return _selfTest; }
    // TEMPORARY diagnostic: last step begin(cfg, adapter) reached, updated
    // synchronously at each stage. One-shot boot-time prints (log_e/log_i,
    // even Serial.printf) are a losing race against hosts whose USB CDC
    // re-enumerates on reset -- this is readable any time after begin()
    // returns, from wherever a periodic heartbeat already lives. Remove once
    // the real begin(adapter) failure is found and fixed.
    const char* debugBeginStep() const { return _debugBeginStep; }
    uint32_t framesSent() const  { return _framesSent; }

    // The running server, so host firmware can add its own routes without this
    // class having to know about them. nullptr before begin(). This is the hook
    // that makes the mirror droppable into other firmware -- the sd_manager
    // that used to use it is gone (ADR 0036), but the seam is the point.
    // Returned as void* so callers that never touch HTTP don't have to pull
    // in ESPAsyncWebServer.h; cast to AsyncWebServer* at the use site.
    void*  serverHandle() const;

    // Remote key sink. Called from the AsyncTCP task, so the callback MUST be
    // non-blocking and must not touch the display -- enqueue only.
    // Signature: (row, col, shift, fn) using hardware matrix coordinates.
    using KeyFn = void (*)(uint8_t row, uint8_t col, bool shift, bool fn);
    void   onKey(KeyFn fn) { _onKey = fn; }

    // Top-edge button sink. BtnG0 (GPIO 0) is the ONLY top button firmware can
    // observe: M5Unified registers exactly one button for board_M5CardputerADV
    // and reads it as (!gpio_in(GPIO_NUM_0)). BtnRst drives EN and cuts power to
    // the SoC, so it is unobservable and unactuatable by definition -- it is not
    // represented here rather than being faked.
    //
    // A GPIO is not a matrix coordinate, so this is a SEPARATE sink instead of a
    // synthetic (row,col). Same task constraints as KeyFn: enqueue only.
    using BtnFn = void (*)(uint8_t btn, uint16_t ms);
    void   onBtn(BtnFn fn) { _onBtn = fn; }

private:
    Config     _cfg;
    IFrameSource* _src = nullptr;
    // False if _src->begin() failed. update()/scanOneTile() skip fetching
    // entirely in that case -- a frame source failure means "no display
    // mirror", not "no server at all". See begin()'s own comment.
    bool       _srcOk = false;
    uint16_t*  _tile   = nullptr;   // RGB565 scratch, 5,400 B
    uint32_t   _crc[kNumTiles] = {0};
    bool       _force[kNumTiles] = {false};
    int        _cursor = 0;
    int        _selfTest = -1;
    uint32_t   _framesSent = 0;
    bool       _ready = false;
    KeyFn      _onKey = nullptr;
    BtnFn      _onBtn = nullptr;
    // Set only by the adapter-driven begin(). When _sink is non-null it takes
    // priority over _onKey/_onBtn at the WebSocket dispatch site -- the two
    // input paths are mutually exclusive per Mirror instance, never merged.
    IInputSink* _sink    = nullptr;
    PortMutex*  _busLock = nullptr; // taken/given around IFrameSource calls, if non-null
    const char* _debugBeginStep = "not started"; // TEMPORARY, see debugBeginStep()

    bool _allocBuffers();            // shadow + tile scratch, shared by both begin() paths
    bool _startServer();             // WiFi/HTTP/WS bring-up, shared by both begin() paths
    bool scanOneTile();             // returns true if the tile changed
    void publishTile(int idx);
};

}  // namespace cmirror

extern cmirror::Mirror CardputerMirror;
