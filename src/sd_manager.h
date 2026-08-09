/*
 * sd_manager — SD card inspection and formatting for the Cardputer ADV.
 *
 * WHY THIS EXISTS
 * ---------------
 * Arduino's SD.begin(..., format_if_empty=true) only formats a card that
 * failed to mount. It cannot reformat a working card, cannot choose FAT32
 * vs FAT16, and gives no progress or diagnostics. This module drives the
 * ESP-IDF FATFS layer directly so a deliberate, explicit format is possible.
 *
 * ADV PIN ASSIGNMENT (verified against M5Unified _pin_table_sd, not guessed):
 *     { board_M5CardputerADV, CLK 40, CMD/MOSI 14, D0/MISO 39, -, -, CS 12 }
 * Identical to the original Cardputer. The display is on SPI3_HOST
 * (MOSI 35 / SCLK 36 / DC 34), so the SD bus below uses SPI2_HOST and the
 * two never contend.
 *
 * THREADING — THE IMPORTANT PART
 * ------------------------------
 * f_mkfs() blocks for seconds and needs a real stack. AsyncWebServer
 * callbacks run on the async_tcp task, which has neither the stack headroom
 * nor the watchdog tolerance for that. So HTTP handlers only *request* a
 * format; sdmgr::update() executes it from loop(). The browser polls
 * /api/sd/status. Never call formatNow() from a request handler.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace sdmgr {

enum class State : uint8_t {
    Absent = 0,     // no card detected
    Mounted,        // mounted, filesystem readable
    Unformatted,    // card present but no mountable filesystem
    Formatting,     // format in progress (update() is working)
    Error           // last operation failed; see detail()
};

enum class FsType : uint8_t { Unknown = 0, Fat12, Fat16, Fat32, ExFat };

struct Info {
    bool     present     = false;
    uint64_t capacityB   = 0;      // total card capacity
    uint64_t totalB      = 0;      // filesystem total
    uint64_t freeB       = 0;      // filesystem free
    FsType   fsType      = FsType::Unknown;
    char     name[8]     = {0};    // card product name
    uint32_t speedKhz    = 0;
    uint16_t sectorSize  = 0;
};

// Mount the card. Safe to call when no card is inserted (returns false and
// leaves state Absent). Idempotent.
bool begin();

// Release the filesystem and the SPI bus device. Call before hot-swapping.
void end();

// Pump deferred work. Call from loop(). This is where a requested format
// actually runs, so a format request costs one loop() iteration of latency.
void update();

// Ask for a format. Returns false if a format is already running or no card
// is present. fat32=false lets FatFs pick (FM_ANY) — needed for cards too
// small for a valid FAT32 (<32MB), which cannot reach 65525 clusters.
bool requestFormat(bool fat32 = true, uint32_t allocUnitBytes = 0);

State       state();
const char* stateName();
const char* detail();          // human-readable last error / status
const Info& info();            // refreshed by begin() and after a format
void        refresh();         // re-stat the filesystem (cheap)
uint8_t     progress();        // 0..100, meaningful while Formatting

const char* fsTypeName(FsType t);

// Register /api/sd/* routes on the mirror's web server. Call after
// CardputerMirror.begin(). Handlers are non-blocking by construction.
void attachRoutes();

}  // namespace sdmgr
