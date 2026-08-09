/*
 * sd_manager implementation.
 *
 * The format path deliberately uses the low-level FATFS API rather than
 * esp_vfs_fat_sdcard_format(), which does not exist in the IDF bundled with
 * this Arduino core (verified: ESP_IDF_VERSION 4.4, and the symbol is absent
 * from esp_vfs_fat.h). The sequence below is what IDF 5.x's implementation
 * does internally, written out explicitly:
 *
 *   pdrv = ff_diskio_get_pdrv_card(card)   // which FATFS volume is this card
 *   esp_vfs_fat_unregister_path(base)      // detach from VFS
 *   f_mount(NULL, drv, 0)                  // unmount, keep diskio registered
 *   f_mkfs(drv, opt, au, work, worklen)    // the actual format
 *   esp_vfs_fat_register(...) + f_mount    // reattach
 *
 * Keeping the diskio registration alive across the format is what makes this
 * work without re-probing the card over SPI.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sd_manager.h"

#include <string.h>
#include <sys/stat.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "esp_log.h"

#include <ESPAsyncWebServer.h>
#include "CardputerMirror.h"

namespace sdmgr {
namespace {

constexpr const char* TAG = "sdmgr";

// Verified against M5Unified _pin_table_sd for board_M5CardputerADV.
constexpr gpio_num_t PIN_CLK  = GPIO_NUM_40;
constexpr gpio_num_t PIN_MOSI = GPIO_NUM_14;
constexpr gpio_num_t PIN_MISO = GPIO_NUM_39;
constexpr gpio_num_t PIN_CS   = GPIO_NUM_12;

// Display owns SPI3_HOST (M5GFX bus_cfg.spi_host for the ADV block).
constexpr spi_host_device_t SD_HOST = SPI2_HOST;

constexpr const char* MOUNT_POINT = "/sd";
constexpr int         MAX_FILES   = 5;

sdmmc_card_t* s_card       = nullptr;
bool          s_busInited  = false;
State         s_state      = State::Absent;
Info          s_info;
char          s_detail[128] = "not initialised";
uint8_t       s_progress   = 0;

// Deferred format request, consumed by update() on the loop() task.
volatile bool     s_formatPending = false;
volatile bool     s_formatFat32   = true;
volatile uint32_t s_formatAu      = 0;

void setDetail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_detail, sizeof(s_detail), fmt, ap);
    va_end(ap);
}

FsType mapFsType(BYTE t)
{
    switch (t) {
        case FS_FAT12: return FsType::Fat12;
        case FS_FAT16: return FsType::Fat16;
        case FS_FAT32: return FsType::Fat32;
        case FS_EXFAT: return FsType::ExFat;
        default:       return FsType::Unknown;
    }
}

// Populate s_info from the mounted volume. Cheap; safe to call often.
void statFilesystem()
{
    s_info.totalB = s_info.freeB = 0;
    s_info.fsType = FsType::Unknown;
    if (!s_card) return;

    FATFS* fs = nullptr;
    DWORD  freeClusters = 0;
    BYTE   pdrv = ff_diskio_get_pdrv_card(s_card);
    char   drv[3] = { (char)('0' + pdrv), ':', 0 };

    if (f_getfree(drv, &freeClusters, &fs) == FR_OK && fs) {
        const uint64_t sect = (uint64_t)fs->csize * s_info.sectorSize;
        s_info.totalB = (uint64_t)(fs->n_fatent - 2) * sect;
        s_info.freeB  = (uint64_t)freeClusters * sect;
        s_info.fsType = mapFsType(fs->fs_type);
    }
}

}  // namespace

const char* fsTypeName(FsType t)
{
    switch (t) {
        case FsType::Fat12: return "FAT12";
        case FsType::Fat16: return "FAT16";
        case FsType::Fat32: return "FAT32";
        case FsType::ExFat: return "exFAT";
        default:            return "none";
    }
}

State       state()    { return s_state; }
const char* detail()   { return s_detail; }
const Info& info()     { return s_info; }
uint8_t     progress() { return s_progress; }

const char* stateName()
{
    switch (s_state) {
        case State::Absent:      return "absent";
        case State::Mounted:     return "mounted";
        case State::Unformatted: return "unformatted";
        case State::Formatting:  return "formatting";
        default:                 return "error";
    }
}

bool begin()
{
    if (s_card) return true;   // already mounted

    if (!s_busInited) {
        spi_bus_config_t bus = {};
        bus.mosi_io_num     = PIN_MOSI;
        bus.miso_io_num     = PIN_MISO;
        bus.sclk_io_num     = PIN_CLK;
        bus.quadwp_io_num   = -1;
        bus.quadhd_io_num   = -1;
        bus.max_transfer_sz = 4000;

        esp_err_t e = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
        // ESP_ERR_INVALID_STATE means someone already initialised this host;
        // that is fine and we simply share it.
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
            s_state = State::Error;
            setDetail("spi_bus_initialize failed: %s", esp_err_to_name(e));
            return false;
        }
        s_busInited = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = SD_HOST;
    host.max_freq_khz = 20000;      // conservative; raise once proven stable

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_CS;
    slot.host_id = SD_HOST;

    esp_vfs_fat_mount_config_t mcfg = {};
    mcfg.format_if_mount_failed = false;   // never format implicitly
    mcfg.max_files              = MAX_FILES;
    mcfg.allocation_unit_size   = 16 * 1024;

    esp_err_t e = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot,
                                          &mcfg, &s_card);
    if (e == ESP_OK) {
        s_state           = State::Mounted;
        s_info.present    = true;
        s_info.capacityB  = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
        s_info.sectorSize = s_card->csd.sector_size;
        s_info.speedKhz   = s_card->max_freq_khz;
        strncpy(s_info.name, s_card->cid.name, sizeof(s_info.name) - 1);
        statFilesystem();
        setDetail("mounted %s, %s", s_info.name, fsTypeName(s_info.fsType));
        ESP_LOGI(TAG, "mounted: %llu B, %s", s_info.capacityB,
                 fsTypeName(s_info.fsType));
        return true;
    }

    s_card = nullptr;
    if (e == ESP_FAIL) {
        // Card responded to SPI probing but has no mountable filesystem.
        // This is the case a formatter exists for, so report it distinctly
        // rather than lumping it in with "no card".
        s_state        = State::Unformatted;
        s_info.present = true;
        setDetail("card present but not mountable - format required");
    } else {
        s_state        = State::Absent;
        s_info         = Info{};
        setDetail("no card (%s)", esp_err_to_name(e));
    }
    return false;
}

void end()
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_card = nullptr;
    }
    s_state = State::Absent;
    s_info  = Info{};
    setDetail("unmounted");
}

void refresh()
{
    if (s_state == State::Formatting) return;
    if (!s_card) { begin(); return; }
    statFilesystem();
}

bool requestFormat(bool fat32, uint32_t allocUnitBytes)
{
    if (s_state == State::Formatting) return false;
    if (!s_info.present && !begin() && s_state != State::Unformatted) return false;

    s_formatFat32   = fat32;
    s_formatAu      = allocUnitBytes;
    s_progress      = 0;
    s_formatPending = true;
    setDetail("format queued");
    return true;
}

namespace {

// Runs on the loop() task only. Blocks for seconds.
void formatNow()
{
    s_state    = State::Formatting;
    s_progress = 5;
    setDetail("formatting...");

    // An unformatted card never mounted, so there is no VFS registration and
    // no diskio drive yet. Mount with format_if_mount_failed so the card gets
    // a filesystem we can then reformat to the requested type.
    if (!s_card) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot         = SD_HOST;
        host.max_freq_khz = 20000;

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.gpio_cs = PIN_CS;
        slot.host_id = SD_HOST;

        esp_vfs_fat_mount_config_t mcfg = {};
        mcfg.format_if_mount_failed = true;
        mcfg.max_files              = MAX_FILES;
        mcfg.allocation_unit_size   = 16 * 1024;

        esp_err_t e = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot,
                                              &mcfg, &s_card);
        if (e != ESP_OK) {
            s_card  = nullptr;
            s_state = State::Error;
            setDetail("format: card unreachable (%s)", esp_err_to_name(e));
            s_progress = 0;
            return;
        }
    }

    s_progress = 15;

    const BYTE pdrv   = ff_diskio_get_pdrv_card(s_card);
    char       drv[3] = { (char)('0' + pdrv), ':', 0 };

    // Detach from VFS and unmount the volume, but leave the diskio driver
    // registered so f_mkfs can still reach the card.
    esp_vfs_fat_unregister_path(MOUNT_POINT);
    f_mount(NULL, drv, 0);
    s_progress = 25;

    // f_mkfs needs a work buffer of at least FF_MAX_SS bytes. Allocate from
    // the heap rather than the task stack: loop()'s stack cannot take 4 KB.
    const size_t workLen = 4096;
    void* work = malloc(workLen);
    if (!work) {
        s_state = State::Error;
        setDetail("format: out of memory for work buffer");
        s_progress = 0;
        return;
    }

    BYTE opt = s_formatFat32 ? FM_FAT32 : FM_ANY;
    FRESULT fr = f_mkfs(drv, opt, s_formatAu, work, workLen);

    // A card too small to reach FAT32's 65525-cluster minimum fails with
    // FR_MKFS_ABORTED. Retrying with FM_ANY lets FatFs choose FAT16/FAT12.
    if (fr == FR_MKFS_ABORTED && s_formatFat32) {
        ESP_LOGW(TAG, "FAT32 rejected (card too small); retrying FM_ANY");
        fr = f_mkfs(drv, FM_ANY, s_formatAu, work, workLen);
    }
    free(work);
    s_progress = 80;

    if (fr != FR_OK) {
        s_state = State::Error;
        setDetail("f_mkfs failed (FRESULT %d)", (int)fr);
        s_progress = 0;
        // Try to get back to a usable state so the next request can retry.
        FATFS* fs = nullptr;
        if (esp_vfs_fat_register(MOUNT_POINT, drv, MAX_FILES, &fs) == ESP_OK) {
            f_mount(fs, drv, 0);
        }
        return;
    }

    // Reattach the freshly formatted volume.
    FATFS* fs = nullptr;
    esp_err_t e = esp_vfs_fat_register(MOUNT_POINT, drv, MAX_FILES, &fs);
    if (e != ESP_OK) {
        s_state = State::Error;
        setDetail("format ok but re-register failed: %s", esp_err_to_name(e));
        s_progress = 0;
        return;
    }
    if (f_mount(fs, drv, 0) != FR_OK) {
        s_state = State::Error;
        setDetail("format ok but remount failed");
        s_progress = 0;
        return;
    }

    s_state = State::Mounted;
    statFilesystem();
    s_progress = 100;
    setDetail("formatted as %s, %llu MB free",
              fsTypeName(s_info.fsType), s_info.freeB / (1024ULL * 1024ULL));
    ESP_LOGI(TAG, "format complete: %s", s_detail);
}

}  // namespace

void update()
{
    if (!s_formatPending) return;
    s_formatPending = false;
    formatNow();
}

}  // namespace sdmgr

// ---------------------------------------------------------------------------
// HTTP surface
// ---------------------------------------------------------------------------
// Every handler below returns immediately. The format endpoint only sets a
// flag; update() does the work on loop(). This is deliberate — see the
// threading note in sd_manager.h.

namespace sdmgr {
namespace {

String infoJson()
{
    const Info& i = info();
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"detail\":\"%s\",\"progress\":%u,"
        "\"present\":%s,\"name\":\"%s\",\"fs\":\"%s\","
        "\"capacity\":%llu,\"total\":%llu,\"free\":%llu,"
        "\"used\":%llu,\"sector\":%u,\"speedKhz\":%u}",
        stateName(), detail(), (unsigned)progress(),
        i.present ? "true" : "false", i.name, fsTypeName(i.fsType),
        (unsigned long long)i.capacityB,
        (unsigned long long)i.totalB,
        (unsigned long long)i.freeB,
        (unsigned long long)(i.totalB > i.freeB ? i.totalB - i.freeB : 0),
        (unsigned)i.sectorSize, (unsigned)i.speedKhz);
    return String(buf);
}

}  // namespace

void attachRoutes()
{
    auto* server = (AsyncWebServer*)CardputerMirror.serverHandle();
    if (!server) {
        ESP_LOGW(TAG, "attachRoutes: server not up; call after Mirror::begin()");
        return;
    }

    server->on("/api/sd/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        refresh();
        req->send(200, "application/json", infoJson());
    });

    server->on("/api/sd/mount", HTTP_POST, [](AsyncWebServerRequest* req) {
        begin();
        req->send(200, "application/json", infoJson());
    });

    server->on("/api/sd/unmount", HTTP_POST, [](AsyncWebServerRequest* req) {
        end();
        req->send(200, "application/json", infoJson());
    });

    // POST /api/sd/format?confirm=FORMAT&fat32=1&au=16384
    // The confirm token is required: a bare POST must never be able to wipe
    // a card, and browsers issue those far too easily.
    server->on("/api/sd/format", HTTP_POST, [](AsyncWebServerRequest* req) {
        String token;
        if (req->hasParam("confirm"))       token = req->getParam("confirm")->value();
        else if (req->hasParam("confirm", true)) token = req->getParam("confirm", true)->value();

        if (token != "FORMAT") {
            req->send(400, "application/json",
                      "{\"error\":\"confirm=FORMAT required\"}");
            return;
        }

        bool fat32 = true;
        if (req->hasParam("fat32")) fat32 = req->getParam("fat32")->value() != "0";
        uint32_t au = 0;
        if (req->hasParam("au")) au = (uint32_t)req->getParam("au")->value().toInt();

        if (!requestFormat(fat32, au)) {
            req->send(409, "application/json",
                      String("{\"error\":\"") + detail() + "\"}");
            return;
        }
        // 202: accepted, not finished. The browser polls /api/sd/status.
        req->send(202, "application/json", infoJson());
    });
}

}  // namespace sdmgr
