/*
 * CardputerMirror — ADR 0002 implementation.
 * SPDX-License-Identifier: MIT
 */
#include "CardputerMirror.h"
#include "Codec.h"
#include "WebAssets.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>

namespace cmirror {

static AsyncWebServer* s_server = nullptr;
static AsyncWebSocket* s_ws     = nullptr;

// ReadbackFrameSource's method bodies live in ReadbackFrameSource.cpp now,
// not here -- Mirror (below) only ever touches a frame source through the
// abstract IFrameSource interface, so this file has no reason to require
// M5Unified.h. See ADR 0039.

// ---------------------------------------------------------------- mirror

static void* allocPreferPsram(size_t n)
{
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_8BIT);  // ADV may have no PSRAM
    return p;
}

bool Mirror::_allocBuffers()
{
    _tile = (uint16_t*)allocPreferPsram(kTilePx * 2);
    if (!_tile) {
        log_e("CardputerMirror: alloc failed (need %u B)", (unsigned)(kTilePx * 2));
        return false;
    }
    return true;
}

bool Mirror::begin() { return begin(Config{}); }

bool Mirror::begin(const Config& cfg)
{
    _cfg = cfg;
    if (!_allocBuffers()) return false;

    static ReadbackFrameSource src;
    _src = &src;
    // A frame source failing to start is NOT fatal to the server -- the
    // WebSocket control channel and remote key/button injection don't
    // depend on it, only tile scanning does (see update()/scanOneTile(),
    // which already no-op safely when _srcOk is false). Splitting these
    // means "the display mirror can't read GRAM" and "remote control
    // doesn't work" are no longer the same failure.
    _srcOk = _src->begin();
    _selfTest = _srcOk ? src.selfTest() : -1;
    log_i("CardputerMirror: readback begin=%d self-test %d%%", _srcOk, _selfTest);

    return _startServer();
}

bool Mirror::begin(IHostAdapter& adapter) { return begin(Config{}, adapter); }

bool Mirror::begin(const Config& cfg, IHostAdapter& adapter)
{
    _debugBeginStep = "start";
    _cfg = cfg;
    if (!_allocBuffers()) {
        _debugBeginStep = "_allocBuffers FAILED";
        return false;
    }
    _debugBeginStep = "_allocBuffers ok";

    // Adapter owns and outlives its frame source / input sink -- Mirror only
    // borrows references, same lifetime contract as the static
    // ReadbackFrameSource in begin(cfg) above.
    adapter.begin();
    _debugBeginStep = "adapter.begin() returned";
    _src     = &adapter.frameSource();
    _sink    = &adapter.inputSink();
    _busLock = adapter.busLock();
    _sink->begin();
    _debugBeginStep = "sink wired, sink->begin() returned";

    // Same reasoning as begin(cfg) above: a frame source failure only means
    // "no display mirror" -- the server, WebSocket, and remote key/button
    // injection through _sink don't touch _src at all, so they still start.
    _srcOk = _src->begin();
    _debugBeginStep = _srcOk ? "_src->begin() ok" : "_src->begin() FAILED (non-fatal)";

    _selfTest = _srcOk ? _src->selfTest() : -1;
    log_i("CardputerMirror: frame source begin=%d self-test %d%% (-1 = adapter has none, or begin failed)",
          _srcOk, _selfTest);

    bool serverOk = _startServer();
    _debugBeginStep = serverOk ? "_startServer() ok" : "_startServer() FAILED";
    return serverOk;
}

bool Mirror::_startServer()
{
    if (_cfg.manageWifi) {
        if (_cfg.ssid) {
            WiFi.mode(WIFI_STA);
            WiFi.begin(_cfg.ssid, _cfg.password);
            for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
        }
        if (!_cfg.ssid || WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_AP);
            WiFi.softAP(_cfg.apSsid, _cfg.apPassword);
        }
    } else {
        log_i("CardputerMirror: manageWifi=false, using caller's connection");
    }

    s_server = new AsyncWebServer(_cfg.port);
    s_ws     = new AsyncWebSocket("/ws");

    s_ws->onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                         AwsEventType type, void*, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            char hello[160];
            int n = snprintf(hello, sizeof(hello),
                "{\"t\":\"hello\",\"w\":%d,\"h\":%d,\"tw\":%d,\"th\":%d,"
                "\"cols\":%d,\"rows\":%d,\"selftest\":%d}",
                kScreenW, kScreenH, kTileW, kTileH, kTileCols, kTileRows, _selfTest);
            client->text(hello, n);
            forceFullFrame();
        } else if (type == WS_EVT_DATA && data && len) {
            // Control messages are tiny; parse without a JSON dependency.
            String msg((const char*)data, len);
            if (msg.indexOf("\"full\"")   >= 0) forceFullFrame();
            if (msg.indexOf("\"swaprb\"") >= 0) setSwapRB(msg.indexOf("true") >= 0);
            if (msg.indexOf("\"invert\"") >= 0) setInvert(msg.indexOf("true") >= 0);
            int b = msg.indexOf("\"budget\":");
            if (b >= 0) {
                uint32_t v = (uint32_t)msg.substring(b + 9).toInt();
                if (v >= 500 && v <= 40000) _cfg.budgetUs = v;
            }

            // Key event: {"t":"key","r":R,"c":C,"shift":bool,"fn":bool}
            // Row/col are the HARDWARE matrix coordinates -- the browser sends
            // what the TCA8418 would have reported, so remote and physical
            // presses converge on one code path (see keyinject.h).
            //
            // This runs on the AsyncTCP task, so it must only ENQUEUE. Touching
            // menu state or the panel here would race the loop task's own
            // drawing, which is why keyinject::post() is the only call made.
            // Top-edge button: {"t":"btn","b":"g0","ms":80}
            // Routed to its own sink, not through the key path -- see onBtn().
            // _sink (adapter-driven begin(), ADR 0038) and _onKey/_onBtn
            // (legacy callback begin()) are mutually exclusive per instance;
            // _sink takes priority when both happen to be set.
            if (msg.indexOf("\"btn\"") >= 0 && (_sink || _onBtn)) {
                const int mi = msg.indexOf("\"ms\":");
                int ms = (mi >= 0) ? msg.substring(mi + 5).toInt() : 80;
                if (ms < 10)   ms = 10;
                if (ms > 2000) ms = 2000;
                if (msg.indexOf("\"g0\"") >= 0) {
                    if (_sink) _sink->injectBtn(0, (uint16_t)ms);
                    else       _onBtn(0, (uint16_t)ms);
                }
            }

            int k = msg.indexOf("\"key\"");
            if (k >= 0 && (_sink || _onKey)) {
                const int ri = msg.indexOf("\"r\":");
                const int ci = msg.indexOf("\"c\":");
                if (ri >= 0 && ci >= 0) {
                    const int r = msg.substring(ri + 4).toInt();
                    const int c = msg.substring(ci + 4).toInt();
                    const bool sh = msg.indexOf("\"shift\":true") >= 0;
                    const bool fn = msg.indexOf("\"fn\":true")    >= 0;
                    if (r >= 0 && r < 4 && c >= 0 && c < 14) {
                        if (_sink) _sink->inject(RemoteKey{(uint8_t)r, (uint8_t)c, sh, fn});
                        else       _onKey((uint8_t)r, (uint8_t)c, sh, fn);
                    }
                }
            }
        }
    });

    s_server->addHandler(s_ws);
    s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* r = req->beginResponse(200, "text/html",
                                       kIndexHtmlGz, kIndexHtmlGzLen);
        r->addHeader("Content-Encoding", "gzip");
        req->send(r);
    });
    // The Stamp-S3A label photo gets its OWN route rather than a base64 data:
    // URI in the page. Base64 costs 33% and is already-compressed bytes, so it
    // would inflate the gzipped page by ~22 KB of incompressible payload on
    // every single load. As a separate resource the browser caches it, and the
    // page stays small. WebP is served as-is -- do NOT add Content-Encoding
    // here, the format is already compressed and double-encoding it would only
    // cost CPU on a device whose whole budget is measured in ms per tile.
    s_server->on("/stamp.webp", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* r = req->beginResponse(200, "image/webp",
                                       kStampWebp, kStampWebpLen);
        r->addHeader("Cache-Control", "public, max-age=31536000, immutable");
        req->send(r);
    });
    // The CARDPUTER ADV panel, same reasoning (ADR 0035). It supersedes a CSS
    // reconstruction that was measured correctly and still read as a drawing.
    s_server->on("/advlabel.webp", HTTP_GET, [](AsyncWebServerRequest* req) {
        auto* r = req->beginResponse(200, "image/webp",
                                       kAdvLabelWebp, kAdvLabelWebpLen);
        r->addHeader("Cache-Control", "public, max-age=31536000, immutable");
        req->send(r);
    });
    s_server->begin();

    forceFullFrame();
    _ready = true;
    return true;
}

void* Mirror::serverHandle() const
{
    return (void*)s_server;
}

void Mirror::forceFullFrame()
{
    for (int i = 0; i < kNumTiles; ++i) { _force[i] = true; _crc[i] = 0; }
}

void Mirror::update()
{
    if (!_ready) return;
    s_ws->cleanupClients();
    if (s_ws->count() == 0) return;   // nothing to do; costs the app nothing
    if (!_srcOk) return;              // no working frame source -- remote control (via the
                                       // WS handler in _startServer()) still works either way

    // Budgeted incremental scan. Runs on the caller's task (loop()), which is
    // what keeps it safe: LGFX is not thread-safe, and borrowing the
    // application's own task inherits its serialization without requiring the
    // application to take a mutex around every draw call.
    const uint32_t t0 = micros();
    while ((micros() - t0) < _cfg.budgetUs) {
        if (!scanOneTile()) break;
    }
}

bool Mirror::scanOneTile()
{
    const int idx = _cursor;
    _cursor = (_cursor + 1) % kNumTiles;

    // ADR 0038 / launcher-adv-mirror ADR 0004: most adapters need no lock
    // here because update() already inherits the caller's own serialization
    // by running on loop() (ADR 0002). _busLock exists for the adapters that
    // don't get that for free -- taken/given only when non-null, so it costs
    // nothing when it isn't needed.
    if (_busLock) xSemaphoreTake((SemaphoreHandle_t)_busLock, portMAX_DELAY);
    const bool ok = _src->fetchTile(idx, _tile);
    if (_busLock) xSemaphoreGive((SemaphoreHandle_t)_busLock);
    if (!ok) return true;

    if (_cfg.swapRB || _cfg.invert) {
        for (size_t i = 0; i < kTilePx; ++i) {
            uint16_t v = _tile[i];
            if (_cfg.swapRB)
                v = (uint16_t)((v & 0x07E0) | ((v & 0x1F) << 11) | (v >> 11));
            if (_cfg.invert) v = (uint16_t)~v;
            _tile[i] = v;
        }
    }

    const uint32_t c = crc32(_tile, kTilePx * 2);
    if (c == _crc[idx] && !_force[idx]) return true;   // unchanged

    _crc[idx]   = c;
    _force[idx] = false;

    publishTile(idx);
    return true;
}

void Mirror::publishTile(int idx)
{
    // Worst case is RAW: 4 (header) + 3 + kTilePx*2.
    static uint8_t out[4 + 3 + kTilePx * 2];
    out[0] = 'T';
    out[1] = (uint8_t)idx;
    out[2] = 0;
    out[3] = 0;
    size_t n = encodeTile(_tile, kTilePx, out + 4, sizeof(out) - 4);
    if (n == 0) return;
    if (s_ws->availableForWriteAll()) {
        // binaryAll() copies `out` into a heap-allocated std::vector
        // (ESPAsyncWebServer's makeSharedBuffer()) before it can queue the
        // send. That allocation is a plain `new`, and this firmware is built
        // with -fno-exceptions, so an allocation failure there does not throw
        // std::bad_alloc -- it calls std::terminate()/abort() and reboots the
        // whole device over one dropped tile. Ask the allocator whether the
        // copy would fit in the largest contiguous free block first, and skip
        // this tile (same graceful-degradation stance as ADR 0041) rather
        // than risk that. ADV units are frequently PSRAM-less, so internal
        // SRAM headroom here can be genuinely tight, not just theoretical.
        if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > (n + 4) + 512) {
            s_ws->binaryAll((const char*)out, n + 4);
            ++_framesSent;
        }
    }
}

String Mirror::ipAddress() const
{
    // Do NOT branch on getMode() == WIFI_AP. The radio is frequently in
    // WIFI_AP_STA -- scanNetworks() calls enableSTA(true) internally, and the
    // fallback AP can end up coexisting with an idle STA interface. An equality
    // test then reports localIP(), which is 0.0.0.0 with no association, so the
    // device claimed "ip=0.0.0.0" for 250 s while its AP was serving happily at
    // 192.168.4.1. The address shown was not just wrong, it hid a working AP.
    //
    // Association state is the thing actually being asked about, so test that.
    if (WiFi.status() == WL_CONNECTED) {
        const IPAddress sta = WiFi.localIP();
        if (sta != IPAddress((uint32_t)0)) return sta.toString();
    }
    const IPAddress ap = WiFi.softAPIP();
    if (ap != IPAddress((uint32_t)0)) return ap.toString();
    return String("0.0.0.0");
}

int Mirror::clientCount() const { return s_ws ? s_ws->count() : 0; }

}  // namespace cmirror

cmirror::Mirror CardputerMirror;
