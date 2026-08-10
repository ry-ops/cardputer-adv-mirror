/*
 * Cardputer ADV browser mirror — example sketch (ADR 0002).
 *
 * Integration into YOUR firmware is the two marked lines.
 * SPDX-License-Identifier: MIT
 */
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <WiFi.h>
#include "CardputerMirror.h"
#include "wifi_manager.h"
#include "wifi_manager_rt.h"
#include "menu.h"
#include "keyinject.h"

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing include/wifi_credentials.h - copy include/wifi_credentials.example.h to it and fill in your network."
#endif

static const wifimgr::Profile kWifiProfiles[] = { WIFI_PROFILES };

/*
 * Placeholder guard.
 *
 * wifi_credentials.h is gitignored, so it gets replaced by hand -- and once it
 * was swapped for a fully REDACTED copy. That built and flashed cleanly, then
 * failed at runtime as an ordinary "no profile joined" SoftAP fallback, which
 * is indistinguishable from a sleeping hotspot or a 5 GHz-only network. The
 * build is the only place this is cheap to catch.
 *
 * constexpr, so a placeholder is a COMPILE error, not a debugging session.
 * strcmp is not constexpr; compare bytes directly.
 */
/*
 * cstarts = "does needle occur at exactly this position", which is what a
 * substring search needs. My first version used full-string EQUALITY here, so
 * cfind could only fire when the whole value equalled the needle -- a
 * placeholder like "PUT_ROTATED_PASSPHRASE_HERE" (needle "PUT_ROTATED") slipped
 * straight through and the build passed. The guard silently did nothing, which
 * is worse than not having one.
 */
static constexpr bool cstarts(const char* h, const char* n)
{
    return *n == '\0' ? true
         : (*h != *n ? false : cstarts(h + 1, n + 1));
}
static constexpr bool cfind(const char* h, const char* n)
{
    return cstarts(h, n) ? true : (*h == '\0' ? false : cfind(h + 1, n));
}
static constexpr bool isPlaceholder(const char* s)
{
    return cfind(s, "REDACTED") || cfind(s, "PUT_ROTATED")
        || cfind(s, "your-network") || cfind(s, "your-password")
        || cfind(s, "backup-network") || cfind(s, "backup-password");
}
/*
 * Check the MACRO's string literals, not kWifiProfiles: that array is `const`,
 * not `constexpr`, so its members are not readable in a constant expression
 * (making it constexpr would change the type the wifimgr API takes). Expanding
 * WIFI_PROFILES inside a constexpr aggregate gives the same literals with no
 * signature change.
 */
namespace credcheck {
    struct P { const char* ssid; const char* password; };
    static constexpr P kAll[] = { WIFI_PROFILES };
    static constexpr size_t kN = sizeof(kAll) / sizeof(kAll[0]);
    static constexpr bool anyPlaceholder(size_t i = 0)
    {
        return i >= kN ? false
             : (isPlaceholder(kAll[i].ssid) || isPlaceholder(kAll[i].password)
                ? true : anyPlaceholder(i + 1));
    }
}
static_assert(!credcheck::anyPlaceholder(),
    "wifi_credentials.h still holds a REDACTED/PUT_ROTATED/example placeholder. "
    "Put the real SSID and passphrase in include/wifi_credentials.h -- spell the "
    "SSID as the AP BROADCASTS it (see the header comment) -- or delete the "
    "unused profile line.");

// mDNS/DHCP name. The hotspot's leases are dynamic, so this is the stable
// way to reach the mirror: http://cardputer.local
static constexpr const char* kHostname = "cardputer";
static wifimgr::Result gWifi;

static void banner()
{
    const int st = CardputerMirror.selfTestScore();
    Serial.println();
    Serial.println("=====================================================");
    Serial.println(" Cardputer ADV Display Mirror  (ADR 0002 / readback)");
    Serial.println("=====================================================");
    switch (gWifi.outcome) {
        case wifimgr::Outcome::Connected:
            Serial.printf("  WiFi          : CONNECTED to '%s'  %ddBm ch%d in %ums\n",
                          gWifi.ssid, (int)gWifi.rssi, (int)gWifi.channel,
                          gWifi.elapsedMs);
            break;
        case wifimgr::Outcome::SoftAP:
            Serial.printf("  WiFi          : SoftAP fallback '%s'\n", gWifi.ssid);
            Serial.printf("    why         : %s\n", gWifi.detail);
            Serial.printf("    scan saw    : %d networks\n", gWifi.scanCount);
            Serial.println("    NOTE        : join this AP from your computer to see the mirror.");
            Serial.println("    iPhone hotspot? enable Settings > Personal Hotspot >");
            Serial.println("                   Maximize Compatibility (forces 2.4GHz).");
            break;
        default:
            Serial.printf("  WiFi          : FAILED - %s\n", gWifi.detail);
            break;
    }
    Serial.printf ("  Open in browser: http://%s\n",
                   gWifi.ip().toString().c_str());
    if (gWifi.mdnsUp)
        Serial.printf ("                 : http://%s.local  (survives DHCP changes)\n",
                       gWifi.hostname);
    Serial.printf ("  Readback self-test: %d%%   <-- THE NUMBER THAT MATTERS\n", st);
    if (st < 0)        Serial.println("    !! self-test could not allocate; inconclusive");
    else if (st >= 95) Serial.println("    OK: 3-wire GRAM readback is reliable. ADR 0002 holds.");
    else if (st >= 60) Serial.println("    MARGINAL: expect artifacts. Try Swap R/B in the browser.");
    else               Serial.println("    FAIL: readback unusable on this unit -> switch to ADR 0001 (panel tee).");
    Serial.printf ("  Free heap     : %u B\n", (unsigned)ESP.getFreeHeap());
    Serial.printf ("  PSRAM         : %u B\n", (unsigned)ESP.getPsramSize());
    Serial.println("  Press 'b' here to reprint this banner.");
    Serial.println("=====================================================");
}

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    // CDC needs a moment to enumerate after the post-upload reset; without
    // this the banner is emitted into a port nobody is listening to yet.
    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    M5.Display.setRotation(1);            // 240x135 landscape
    M5.Display.setTextSize(1);
    M5.Display.fillScreen(TFT_BLACK);

    // Bring WiFi up BEFORE the mirror, so the mirror binds its server to a
    // radio that is already in its final state.
    const size_t kNumProfiles = sizeof(kWifiProfiles) / sizeof(kWifiProfiles[0]);
    gWifi = wifimgr::begin(kWifiProfiles, kNumProfiles,
                           WIFI_AP_SSID, WIFI_AP_PASS, kHostname);
    // Hand the same arguments to the runtime layer so the Network screen can
    // re-run this without a reboot.
    wifimgr::remember(kWifiProfiles, kNumProfiles,
                      WIFI_AP_SSID, WIFI_AP_PASS, kHostname);
    wifimgr::setLast(gWifi);

    cmirror::Config mc;
    mc.manageWifi = false;                // wifimgr owns the radio
    mc.budgetUs   = 4500;                 // ~1 tile (4.05 ms) per loop()

    CardputerMirror.begin(mc);            // <-- line 1


    // The boot status text is gone: every value it printed now lives on a menu
    // screen that stays current, instead of a snapshot that goes stale the
    // moment anything changes.
    keyinject::begin();
    CardputerMirror.onKey(keyinject::post_trampoline);
    // BtnG0 gets its own sink: it is a GPIO, not a matrix coordinate.
    CardputerMirror.onBtn(keyinject::postBtn_trampoline);
    menu::begin();

    banner();
}

// Repeats every 2 s so `pio device monitor` shows something even when it
// attaches long after boot — with ARDUINO_USB_CDC_ON_BOOT=1 the port
// re-enumerates on reset, so one-shot boot prints are routinely missed.
static void heartbeat()
{
    static uint32_t last = 0;
    if (millis() - last < 2000) return;
    last = millis();
    // keys= is the device's own count of remote presses APPLIED, and it is the
    // number that settles "is control broken". The browser's "keys sent" is
    // the browser's opinion; this is the device's. If the browser counts a
    // press and this does not move, the packet never arrived -- which is a
    // link problem. If both move and nothing happens on screen, it is a menu
    // problem. Without this pair the two are indistinguishable, and that
    // ambiguity is what made "control only works over USB" take this long.
    Serial.printf("[%6lus] ip=%-15s clients=%d tiles=%lu keys=%lu/%lu rssi=%d heap=%u\n",
                  millis() / 1000,
                  CardputerMirror.ipAddress().c_str(),
                  CardputerMirror.clientCount(),
                  (unsigned long)CardputerMirror.framesSent(),
                  (unsigned long)keyinject::applied(),
                  (unsigned long)keyinject::dropped(),
                  (int)WiFi.RSSI(),
                  (unsigned)ESP.getFreeHeap());
}

void loop()
{
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto st = M5Cardputer.Keyboard.keysState();
        // The menu gets first refusal. It returns false for anything it does
        // not use, so a future full-screen app can still receive typing.
        // Log EVERY physical key with its real matrix coordinate before the
        // menu gets a say. keyList() is the same (col,row) the TCA8418
        // reported, so a physical press and a browser press produce identical
        // rows on the Key Test screen -- which is what makes that screen able
        // to tell a broken remote path from a key the menu simply ignores.
        for (const auto& kp : M5Cardputer.Keyboard.keyList()) {
            const auto kv = M5Cardputer.Keyboard.getKeyValue(kp);
            menu::recordKey(st.shift ? kv.value_second : kv.value_first,
                            (uint8_t)kp.y, (uint8_t)kp.x,
                            st.shift, st.fn, false);
        }
        menu::handleKey(st.word.data(), st.word.size(),
                        st.enter, st.del, st.tab, st.fn);
    }

    // Press 'b' in the serial monitor to reprint the banner.
    if (Serial.available() && Serial.read() == 'b') banner();

    heartbeat();
    keyinject::update();                  // apply remote keys BEFORE repaint
    menu::update();                       // repaint before the mirror reads GRAM
    CardputerMirror.update();             // <-- line 2
    delay(2);
}
