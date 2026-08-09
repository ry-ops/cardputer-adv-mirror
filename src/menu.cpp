#include "menu.h"
#include <string.h>
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include "wifi_manager_rt.h"
#include "sd_manager.h"
#include "keyinject.h"
#include "CardputerMirror.h"

namespace menu {
namespace {

// 240x135 at font 1 (6x8 px) = 40 cols x 16 rows. Two rows go to the title and
// the hint bar, leaving 14 for content -- that is what caps the list lengths
// below, and why the scan screen scrolls rather than paging.
constexpr int kCharW = 6, kCharH = 8;
constexpr int kCols  = 240 / kCharW;   // 40
constexpr int kBodyY = 12;
constexpr int kBodyRows = 13;

enum class Screen : uint8_t { Home, Network, Scan, Storage, System, Mirror, Keys };

Screen   s_screen  = Screen::Home;
int      s_sel     = 0;
bool     s_dirty   = true;
uint32_t s_lastLive = 0;
char     s_toast[48] = {0};
uint32_t s_toastUntil = 0;

// ---- Key test instrument -------------------------------------------------
// WHY THIS EXISTS: the menu acts on 8 of the 56 keys. Every other key was
// being delivered correctly and then silently discarded, which is
// indistinguishable from "the key never arrived". There was no way to tell a
// broken input path from a key the menu simply ignores -- so this screen
// echoes EVERY key it receives, whatever the menu does with it afterwards.
struct KeyEvt {
    char     ch;         // decoded character (0 for pure modifiers)
    uint8_t  row, col;   // hardware matrix coordinate
    bool     shift, fn;
    bool     remote;     // true = arrived over WebSocket, false = physical
    uint32_t at;         // millis()
};
constexpr int kKeyLogN = 8;
KeyEvt  s_keyLog[kKeyLogN];
int     s_keyLogN   = 0;   // total ever recorded (also the write cursor mod N)
uint32_t s_keysSeen = 0;

// Which of the 56 matrix positions have been pressed at least once. This is
// the actual coverage answer: "have all 56 keys been proven to work".
uint16_t s_seenMask[4] = {0, 0, 0, 0};   // bit c of row r

// Scan results live here between the scan and the draw; 24 entries is more
// than fits on screen, and scrolling beyond that is not worth the RAM.
wifimgr::ScanEntry s_scan[24];
int  s_scanN = 0;
int  s_scanTop = 0;
bool s_scanRun = false;

void toast(const char* msg, uint32_t ms = 2500)
{
    strncpy(s_toast, msg, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = 0;
    s_toastUntil = millis() + ms;
    s_dirty = true;
}

void hdr(const char* title)
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextFont(1);
    M5.Display.setTextSize(1);
    M5.Display.fillRect(0, 0, 240, 10, 0x18E3);
    M5.Display.setTextColor(TFT_CYAN, 0x18E3);
    M5.Display.setCursor(2, 1);
    M5.Display.print(title);

    // Battery is the one status worth carrying on every screen: this device is
    // routinely run off the cable and there is no other indicator.
    const int b = M5.Power.getBatteryLevel();
    char bs[12];
    snprintf(bs, sizeof(bs), "%3d%%", b);
    M5.Display.setTextColor(b < 20 ? TFT_RED : (b < 50 ? TFT_YELLOW : TFT_GREEN), 0x18E3);
    M5.Display.setCursor(240 - 4 * kCharW - 2, 1);
    M5.Display.print(bs);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void hint(const char* h)
{
    M5.Display.fillRect(0, 135 - 10, 240, 10, 0x10A2);
    M5.Display.setTextColor(TFT_DARKGREY, 0x10A2);
    M5.Display.setCursor(2, 135 - 9);
    if (s_toast[0] && millis() < s_toastUntil) {
        M5.Display.setTextColor(TFT_YELLOW, 0x10A2);
        M5.Display.print(s_toast);
    } else {
        M5.Display.print(h);
    }
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void row(int i, const char* text, bool sel, uint16_t fg = TFT_WHITE)
{
    const int y = kBodyY + i * (kCharH + 2);
    if (sel) {
        M5.Display.fillRect(0, y - 1, 240, kCharH + 2, 0x001F);
        M5.Display.setTextColor(TFT_WHITE, 0x001F);
    } else {
        M5.Display.setTextColor(fg, TFT_BLACK);
    }
    M5.Display.setCursor(4, y);
    M5.Display.print(text);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

// ---------------------------------------------------------------- Home -----
const char* kHomeItems[] = { "Network", "Storage", "System", "Mirror", "Key Test" };
constexpr int kHomeN = 5;

void drawHome()
{
    hdr("Cardputer ADV");
    const auto& w = wifimgr::last();
    char line[48];

    for (int i = 0; i < kHomeN; ++i) {
        const char* sub = "";
        char tmp[48];
        if (i == 0) {
            // Show the state, not just the name -- "AP" here is the single most
            // useful fact on the whole device and it used to require serial.
            snprintf(tmp, sizeof(tmp), "Network      %s",
                     w.ok() ? (w.ssid ? w.ssid : "connected") : "AP MODE");
            sub = tmp;
        } else if (i == 1) {
            const auto& si = sdmgr::info();
            snprintf(tmp, sizeof(tmp), "Storage      %s",
                     si.present ? sdmgr::fsTypeName(si.fsType) : "no card");
            sub = tmp;
        } else if (i == 2) {
            snprintf(tmp, sizeof(tmp), "System       %ukB free",
                     (unsigned)(ESP.getFreeHeap() / 1024));
            sub = tmp;
        } else {
            snprintf(tmp, sizeof(tmp), "Mirror       %d client%s",
                     CardputerMirror.clientCount(),
                     CardputerMirror.clientCount() == 1 ? "" : "s");
            sub = tmp;
        }
        strncpy(line, sub, sizeof(line) - 1); line[sizeof(line) - 1] = 0;
        row(i, line, i == s_sel,
            (i == 0 && !w.ok()) ? TFT_YELLOW : TFT_WHITE);
    }

    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, kBodyY + 5 * (kCharH + 2) + 4);
    snprintf(line, sizeof(line), "http://%s", w.ip().toString().c_str());
    M5.Display.print(line);
    if (w.mdnsUp) {
        M5.Display.setCursor(4, kBodyY + 6 * (kCharH + 2) + 4);
        M5.Display.printf("or http://%s.local", w.hostname ? w.hostname : "cardputer");
    }
    hint("up/dn select  ENT open");
}

// ------------------------------------------------------------- Network -----
void drawNetwork()
{
    hdr("Network");
    const auto& w = wifimgr::last();
    char l[48];
    int r = 0;

    M5.Display.setTextColor(w.ok() ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    M5.Display.setCursor(4, kBodyY);
    M5.Display.print(w.ok() ? "STATE  connected" : "STATE  SoftAP fallback");
    r = 1;
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(l, sizeof(l), "SSID   %s%s", w.ssid ? w.ssid : "-",
             w.openNet ? "  [OPEN]" : "");                                 row(r++, l, false);
    snprintf(l, sizeof(l), "IP     %s", w.ip().toString().c_str());       row(r++, l, false);
    if (w.ok()) {
        snprintf(l, sizeof(l), "SIGNAL %ddBm  ch%d", (int)w.rssi, (int)w.channel);
        row(r++, l, false);
    } else {
        // The failure reason, on the device. This is the line that previously
        // only existed in a serial log nobody could see at the time.
        snprintf(l, sizeof(l), "WHY    %s", w.detail && *w.detail ? w.detail : "-");
        row(r++, l, false, TFT_YELLOW);
        snprintf(l, sizeof(l), "SCAN   saw %d networks", w.scanCount);
        row(r++, l, false);
        // Seeing many networks but not yours rules out a broken radio and
        // leaves band as the leading explanation.
        // Associating with an open network is not the same as having a usable
        // network. Corporate open SSIDs are commonly gated by a captive portal,
        // which the device cannot sign in to. Say it here rather than let a
        // green status imply working connectivity.
        if (w.openNet && w.outcome == wifimgr::Outcome::Connected)
            row(r++, "NOTE   open net; captive portal may block", false, TFT_YELLOW);
        if (w.scanCount > 3 && !w.seenInScan)
            row(r++, "HINT   many APs seen, yours absent = 5GHz?", false, TFT_CYAN);
    }
    snprintf(l, sizeof(l), "mDNS   %s", w.mdnsUp ? "up" : "down");        row(r++, l, false);

    r++;
    row(r++, "[ Retry connect ]", s_sel == 0, TFT_CYAN);
    row(r++, "[ Scan networks ]", s_sel == 1, TFT_CYAN);
    hint("ENT run   ` back");
}

void drawScan()
{
    hdr("Scan");
    if (s_scanRun) {
        M5.Display.setCursor(4, kBodyY + 20);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.print("scanning 2.4 GHz ...");
        hint("");
        return;
    }
    if (s_scanN <= 0) {
        M5.Display.setCursor(4, kBodyY + 20);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.print(s_scanN == 0 ? "no networks found" : "scan failed");
        hint("ENT rescan   ` back");
        return;
    }
    char l[48];

    // VERDICT LINE. "I don't see it" from a human reading a 24-row list on a
    // 40-column screen is not the same claim as "it is not being received", and
    // this project has already burned several cycles on that ambiguity. State
    // the answer outright instead of making anyone visually search.
    int found = -1;
    for (int i = 0; i < s_scanN; ++i) if (s_scan[i].match) { found = i; break; }
    const char* want = wifimgr::targetSsid();

    M5.Display.setCursor(2, kBodyY - 1);
    if (found >= 0) {
        // Show the BROADCAST spelling, not the configured one. Displaying what
        // was typed into wifi_credentials.h tells the reader nothing about the
        // air: it renders identically whether case-folding is working or not.
        // The whole point of this line is to report what the radio received.
        const bool caseDiffers = strcmp(s_scan[found].ssid, want) != 0;
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        snprintf(l, sizeof(l), "%.12s FOUND ch%d %ddBm%s", s_scan[found].ssid,
                 (int)s_scan[found].channel, (int)s_scan[found].rssi,
                 caseDiffers ? " ~case" : "");
    } else {
        // Nothing matched, so there is no broadcast form to show -- the
        // configured spelling is the only honest thing to print here.
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        snprintf(l, sizeof(l), "%.12s NOT RECEIVED (%d APs)", want, s_scanN);
    }
    M5.Display.print(l);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    const int listTop = 1;   // verdict occupies row 0
    const int rows    = kBodyRows - listTop;
    const int shown = (s_scanN - s_scanTop) < rows ? (s_scanN - s_scanTop) : rows;
    for (int i = 0; i < shown; ++i) {
        const auto& e = s_scan[s_scanTop + i];
        // Truncate to fit 40 cols: 4 (rssi) + 5 (ch) + name.
        snprintf(l, sizeof(l), "%4d c%-3d %-6s %-15.15s%s",
                 (int)e.rssi, (int)e.channel, wifimgr::authName(e.auth),
                 e.ssid, e.match ? " *" : "");
        // The asterisk marks the configured SSID matched case-insensitively --
        // the exact bug from ADR 0007, now visible without a serial cable.
        row(i + listTop, l, false, e.match ? TFT_GREEN : TFT_WHITE);
    }
    // State what this scan CANNOT see. An empty result and a 5 GHz-only network
    // look identical here, and that ambiguity has cost this project three
    // flashes. The radio is 2.4 GHz only -- say so on the screen, every time.
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(2, 135 - 19);
    M5.Display.printf("2.4GHz only - a 5GHz AP CANNOT appear");
    hint("ENT rescan  up/dn scroll  ` back");
}

// ------------------------------------------------------------- Storage -----
void drawStorage()
{
    hdr("Storage");
    // MY BUG: this used to call sdmgr::refresh() unconditionally on every
    // repaint. refresh() calls begin() when no card is mounted, and begin() is
    // a blocking SD probe -- so with no card inserted, merely LOOKING at this
    // screen spawned a remount attempt several times a second. That is the
    // sdmmc_init_ocr 0x107 / send_if_cond 0x108 flood in the serial log.
    //
    // A draw function must not drive hardware. Probe on a slow cadence, and
    // only re-probe an ABSENT card -- a mounted one is restatted cheaply.
    static uint32_t s_lastProbe = 0;
    const uint32_t nowMs = millis();
    if (sdmgr::info().present) {
        if (nowMs - s_lastProbe > 2000) { s_lastProbe = nowMs; sdmgr::refresh(); }
    } else if (nowMs - s_lastProbe > 10000) {
        s_lastProbe = nowMs; sdmgr::refresh();
    }
    const auto& si = sdmgr::info();
    char l[48];
    int r = 0;

    M5.Display.setCursor(4, kBodyY);
    M5.Display.setTextColor(si.present ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.print(si.present ? "CARD   present" : "CARD   not detected");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    r = 1;

    if (si.present) {
        snprintf(l, sizeof(l), "NAME   %s", si.name);                       row(r++, l, false);
        snprintf(l, sizeof(l), "FS     %s", sdmgr::fsTypeName(si.fsType));  row(r++, l, false);
        snprintf(l, sizeof(l), "SIZE   %llu MB",
                 (unsigned long long)(si.capacityB / (1024ULL * 1024ULL)));  row(r++, l, false);
        if (si.totalB) {
            snprintf(l, sizeof(l), "FREE   %llu of %llu MB",
                     (unsigned long long)(si.freeB  / (1024ULL * 1024ULL)),
                     (unsigned long long)(si.totalB / (1024ULL * 1024ULL)));
            row(r++, l, false);
        }
    } else {
        snprintf(l, sizeof(l), "%s", sdmgr::detail());                       row(r++, l, false, TFT_YELLOW);
    }
    if (sdmgr::state() == sdmgr::State::Formatting) {
        snprintf(l, sizeof(l), "FORMATTING %u%%", (unsigned)sdmgr::progress());
        row(r++, l, false, TFT_YELLOW);
    }

    r++;
    row(r++, "[ Remount ]", s_sel == 0, TFT_CYAN);
    // Formatting is deliberately NOT offered here. A destructive action needs a
    // confirmation the 4-row keyboard cannot make un-fat-fingerable, and the
    // browser already has a two-step confirm. See ADR 0008.
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, kBodyY + (r + 1) * (kCharH + 2));
    M5.Display.print("format: browser only (destructive)");
    hint("ENT run   ` back");
}

// -------------------------------------------------------------- System -----
void drawSystem()
{
    hdr("System");
    char l[48];
    int r = 0;
    snprintf(l, sizeof(l), "HEAP   %u kB free / %u kB",
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getHeapSize() / 1024));                 row(r++, l, false);
    snprintf(l, sizeof(l), "PSRAM  %u kB",
             (unsigned)(ESP.getPsramSize() / 1024));                row(r++, l, false);
    snprintf(l, sizeof(l), "FLASH  %u kB", (unsigned)(ESP.getFlashChipSize() / 1024));
    row(r++, l, false);
    snprintf(l, sizeof(l), "CPU    %u MHz", (unsigned)ESP.getCpuFreqMHz());
    row(r++, l, false);
    const uint32_t up = millis() / 1000;
    snprintf(l, sizeof(l), "UPTIME %luh %02lum %02lus",
             (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60),
             (unsigned long)(up % 60));                             row(r++, l, false);
    snprintf(l, sizeof(l), "BATT   %d%%  %s", M5.Power.getBatteryLevel(),
             M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging
                 ? "charging" : "on battery");                      row(r++, l, false);
    snprintf(l, sizeof(l), "IMU    %s",
             M5.Imu.getType() == m5::imu_bmi270 ? "BMI270" :
             (M5.Imu.getType() == m5::imu_none  ? "none" : "other"));
    row(r++, l, false);
    snprintf(l, sizeof(l), "SELFTST %d%% readback", CardputerMirror.selfTestScore());
    row(r++, l, false, CardputerMirror.selfTestScore() >= 95 ? TFT_GREEN : TFT_YELLOW);

    r++;
    row(r++, "[ Reboot ]", s_sel == 0, TFT_CYAN);
    hint("ENT run   ` back");
}

// -------------------------------------------------------------- Mirror -----
void drawMirror()
{
    hdr("Mirror");
    char l[48];
    int r = 0;
    const auto& w = wifimgr::last();
    snprintf(l, sizeof(l), "URL    http://%s", w.ip().toString().c_str()); row(r++, l, false, TFT_CYAN);
    snprintf(l, sizeof(l), "CLIENT %d connected", CardputerMirror.clientCount());
    row(r++, l, false, CardputerMirror.clientCount() ? TFT_GREEN : TFT_DARKGREY);
    snprintf(l, sizeof(l), "TILES  %lu sent", (unsigned long)CardputerMirror.framesSent());
    row(r++, l, false);
    snprintf(l, sizeof(l), "SELF   %d%%", CardputerMirror.selfTestScore());
    row(r++, l, false);
    r++;
    row(r++, "[ Force full frame ]", s_sel == 0, TFT_CYAN);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(4, kBodyY + (r + 1) * (kCharH + 2));
    M5.Display.print("this menu is what the browser mirrors");
    hint("ENT run   ` back");
}

// ------------------------------------------------------------------ Keys ----
// Printable name for a decoded character, including the control codes the
// vendor map stores as macros.
const char* keyName(char c, char* tmp, size_t n)
{
    switch ((uint8_t)c) {
        case 0x28: return "enter";
        case 0x2a: return "del";
        case 0x2b: return "tab";
        case 0x00: return "opt";
        case 0xff: return "fn";
        case 0x80: return "ctrl";
        case 0x81: return "shift";
        case 0x82: return "alt";
        case ' ':  return "space";
        default: break;
    }
    if ((uint8_t)c >= 0x21 && (uint8_t)c < 0x7f) { tmp[0] = c; tmp[1] = 0; return tmp; }
    snprintf(tmp, n, "0x%02x", (uint8_t)c);
    return tmp;
}

// The four dedicated arrow keys, by matrix coordinate. Verified against
// Keyboard.h and the TCA8418 7x8 -> 4x14 remap (56 raw positions map
// bijectively onto 56 cells, so there is no 57th key hiding anywhere).
const char* arrowName(uint8_t r, uint8_t c)
{
    if (r == 2 && c == 11) return "UP";
    if (r == 3 && c == 10) return "LEFT";
    if (r == 3 && c == 11) return "DOWN";
    if (r == 3 && c == 12) return "RIGHT";
    return nullptr;
}

void drawKeys()
{
    hdr("Key Test");
    char l[64], nm[8];
    int r = 0;

    // Coverage first: this is the answer to "do all 56 keys work".
    int seen = 0;
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 14; ++c)
            if (s_seenMask[i] & (1u << c)) ++seen;
    snprintf(l, sizeof(l), "coverage %d/56   events %u", seen, (unsigned)s_keysSeen);
    row(r++, l, false, seen == 56 ? TFT_GREEN : TFT_CYAN);

    // A nonzero drop count attributes a coverage gap to queue overflow rather
    // than to a broken key -- without it the two are indistinguishable.
    if (keyinject::dropped()) {
        snprintf(l, sizeof(l), "DROPPED %u (browser too fast)",
                 (unsigned)keyinject::dropped());
        row(r++, l, false, TFT_RED);
    }

    if (s_keyLogN == 0) {
        row(r++, "press any key - physical or browser", false, TFT_DARKGREY);
        row(r++, "every key is echoed, even ones the", false, TFT_DARKGREY);
        row(r++, "menu ignores.  ` = back", false, TFT_DARKGREY);
        return;
    }

    // Most recent first.
    const int show = (s_keyLogN < kKeyLogN) ? s_keyLogN : kKeyLogN;
    for (int i = 0; i < show && r < kBodyRows; ++i) {
        const int idx = (s_keyLogN - 1 - i + kKeyLogN * 2) % kKeyLogN;
        const KeyEvt& e = s_keyLog[idx];
        // Arrows are identified by COORDINATE, not character: ';' at (2,11) is
        // the up arrow, but ';' typed via shift elsewhere would not be. The
        // coordinate is the only unambiguous discriminator.
        const char* arrow = arrowName(e.row, e.col);
        snprintf(l, sizeof(l), "%s r%d c%-2d %-6s%s%s",
                 e.remote ? "WEB" : "KEY", e.row, e.col,
                 arrow ? arrow : keyName(e.ch, nm, sizeof(nm)),
                 e.shift ? " +sh" : "", e.fn ? " +fn" : "");
        row(r++, l, false, i == 0 ? TFT_WHITE : TFT_DARKGREY);
    }
}

int selCount()
{
    switch (s_screen) {
        case Screen::Home:    return kHomeN;
        case Screen::Network: return 2;
        default:              return 1;
    }
}

void draw()
{
    switch (s_screen) {
        case Screen::Home:    drawHome();    break;
        case Screen::Network: drawNetwork(); break;
        case Screen::Scan:    drawScan();    break;
        case Screen::Storage: drawStorage(); break;
        case Screen::System:  drawSystem();  break;
        case Screen::Mirror:  drawMirror();  break;
        case Screen::Keys:    drawKeys();    break;
    }
    s_dirty = false;
}

void runScan()
{
    s_scanRun = true; draw();               // paint "scanning..." before blocking
    // MY BUG: this passed last().ssid, which in SoftAP fallback is the
    // FALLBACK AP's own name ("CardputerADV"), not the network we want. So the
    // match marker compared every scan result against the wrong string and
    // could never light up -- even with the target sitting right there in the
    // list. Ask for what is configured.
    s_scanN  = wifimgr::scanOnce(s_scan, 24, wifimgr::targetSsid());
    s_scanTop = 0;
    s_scanRun = false;
    s_dirty = true;
}

void activate()
{
    switch (s_screen) {
        case Screen::Home:
            s_screen = (s_sel == 0) ? Screen::Network
                     : (s_sel == 1) ? Screen::Storage
                     : (s_sel == 2) ? Screen::System
                     : (s_sel == 3) ? Screen::Mirror : Screen::Keys;
            s_sel = 0;
            break;
        case Screen::Network:
            if (s_sel == 0) {
                toast("reconnecting...", 8000); draw();
                const auto r = wifimgr::retry();
                toast(r.ok() ? "connected" : "still no join", 3000);
            } else {
                s_screen = Screen::Scan; s_sel = 0; runScan();
            }
            break;
        case Screen::Scan:    runScan(); break;
        case Screen::Storage:
            toast("remounting...", 4000); draw();
            // Explicit user action bypasses the probe throttle above.
            toast(sdmgr::begin() ? "mounted" : sdmgr::detail(), 3000);
            break;
        case Screen::System:
            toast("rebooting", 800); draw(); delay(400); ESP.restart();
            break;
        case Screen::Mirror:
            CardputerMirror.forceFullFrame();
            toast("full frame queued", 2000);
            break;
        case Screen::Keys:
            // Enter clears, so coverage can be re-measured from scratch.
            s_keyLogN = 0; s_keysSeen = 0;
            for (int i = 0; i < 4; ++i) s_seenMask[i] = 0;
            toast("cleared", 1200);
            break;
    }
    s_dirty = true;
}

}  // namespace

void begin() { s_screen = Screen::Home; s_sel = 0; s_dirty = true; draw(); }
bool active() { return true; }

bool keysScreenActive() { return s_screen == Screen::Keys; }

void recordKey(char ch, uint8_t row_, uint8_t col_,
               bool shift, bool fn, bool remote)
{
    if (row_ < 4 && col_ < 14) s_seenMask[row_] |= (uint16_t)(1u << col_);
    KeyEvt& e = s_keyLog[s_keyLogN % kKeyLogN];
    e.ch = ch; e.row = row_; e.col = col_;
    e.shift = shift; e.fn = fn; e.remote = remote; e.at = millis();
    ++s_keyLogN;
    ++s_keysSeen;
    // Only repaint if this screen is actually showing it. Marking dirty from a
    // background screen would retransmit tiles for a change nobody can see --
    // the mirror only sends what changed, and that is worth preserving.
    if (s_screen == Screen::Keys) s_dirty = true;
}

bool handleKey(const char* word, size_t wordLen,
               bool enter, bool del, bool tab, bool fn)
{
    // The ADV has FOUR DEDICATED ARROW KEYS -- a physical inverted-T, silkscreened
    // with arrows, at matrix (2,11) up / (3,10) left / (3,11) down / (3,12) right.
    // They report as ';' ',' '.' '/' because M5Cardputer reuses the original
    // Cardputer's _key_value_map; the keycap legend and the reported character
    // differ. That is a library detail, NOT an fn chord -- no modifier is
    // involved and none is checked here.
    //
    // (An earlier comment here claimed the ADV had no arrow keys and that fn was
    //  required. Both were wrong. The code was always right; only the belief was
    //  wrong -- which is why it survived so long. See ADR 0019.)
    //
    // w/s/a/d are kept as aliases: harmless, and useful over a serial terminal.
    // On the Keys screen every key is a specimen, not a command -- otherwise
    // pressing '.' to test it would navigate away from the screen under test.
    // Only '`' (back) and enter (clear) keep their meaning there.
    if (s_screen == Screen::Keys) {
        if (enter) { activate(); return true; }
        for (size_t i = 0; i < wordLen; ++i)
            if (word[i] == '`') {
                s_screen = Screen::Home; s_sel = 0; s_dirty = true; return true;
            }
        if (del) { s_screen = Screen::Home; s_sel = 0; s_dirty = true; return true; }
        return true;   // consumed: logged, deliberately inert
    }

    bool up = false, dn = false, back = false, fwd = false;
    for (size_t i = 0; i < wordLen; ++i) {
        const char c = word[i];
        if (c == ';' || c == 'w') up = true;
        if (c == '.' || c == 's') dn = true;
        // On a single-column menu there is no horizontal axis, so left/right take
        // the meaning they have in every hierarchical menu: left = out, right = in.
        if (c == '`')             back = true;
        if (c == ',' || c == 'a') back = true;
        if (c == '/' || c == 'd') fwd  = true;
    }
    if (del) back = true;

    const int n = selCount();
    if (up || dn) {
        if (s_screen == Screen::Scan) {
            s_scanTop += dn ? 1 : -1;
            const int rows   = kBodyRows - 1;   // verdict line occupies one row
            const int maxTop = (s_scanN > rows) ? (s_scanN - rows) : 0;
            if (s_scanTop < 0) s_scanTop = 0;
            if (s_scanTop > maxTop) s_scanTop = maxTop;
        } else {
            s_sel += dn ? 1 : -1;
            if (s_sel < 0)  s_sel = n - 1;
            if (s_sel >= n) s_sel = 0;
        }
        s_dirty = true;
        return true;
    }
    if (back) {
        if (s_screen == Screen::Scan)      { s_screen = Screen::Network; s_sel = 1; }
        else if (s_screen != Screen::Home) { s_screen = Screen::Home;    s_sel = 0; }
        s_dirty = true;
        return true;
    }
    if (enter || fwd) { activate(); return true; }
    if (tab || fn) return true;
    return false;
}

// Fingerprint of everything a screen displays that can change on its own.
// Repainting on a timer instead would mark all 12 tiles dirty every second and
// make the mirror retransmit an identical screen forever; the mirror's whole
// design is that it only sends what changed. So repaint on CHANGE, not on time.
static uint32_t liveHash()
{
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)s_screen);
    mix((uint32_t)s_sel);
    mix((uint32_t)s_scanTop);
    mix((uint32_t)M5.Power.getBatteryLevel());
    mix((uint32_t)CardputerMirror.clientCount());
    switch (s_screen) {
        case Screen::System:
            mix((uint32_t)(ESP.getFreeHeap() / 1024));
            mix((uint32_t)(millis() / 1000));          // uptime ticks every 1 s
            break;
        case Screen::Mirror:
            mix((uint32_t)CardputerMirror.framesSent());
            break;
        case Screen::Storage:
            mix((uint32_t)sdmgr::state());
            mix((uint32_t)sdmgr::progress());
            break;
        case Screen::Home:
            mix((uint32_t)(ESP.getFreeHeap() / 1024));
            break;
        default: break;
    }
    return h;
}

void update()
{
    const uint32_t now = millis();
    if (s_toast[0] && now > s_toastUntil) { s_toast[0] = 0; s_dirty = true; }

    // Poll the fingerprint at 4 Hz -- cheap, and far below the cost of the
    // repaint it usually avoids.
    if (now - s_lastLive > 250) {
        s_lastLive = now;
        static uint32_t prev = 0;
        const uint32_t h = liveHash();
        if (h != prev) { prev = h; s_dirty = true; }
    }
    if (s_dirty) draw();
}

}  // namespace menu
