#include "keyinject.h"
#include "menu.h"
#include <M5Cardputer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace keyinject {
namespace {

// One queue for both kinds so ordering between a key and a button press is
// preserved -- two queues would let a G0 press overtake keys typed before it.
enum class Kind : uint8_t { Key, Btn };
struct Evt { Kind kind; uint8_t row, col; bool shift, fn; uint16_t ms; };

QueueHandle_t s_q       = nullptr;
uint32_t      s_applied = 0;
uint32_t      s_dropped = 0;

// The vendor keymap is the single source of truth for what a coordinate means.
// _key_value_map lives in M5Cardputer's Keyboard.h and is indexed [row][col],
// confirmed by Keyboard.cpp:47 -- _key_value_map[keyCoor.y][keyCoor.x] with
// y=row, x=col. Reusing it means the web layout cannot disagree with hardware.
char valueAt(uint8_t row, uint8_t col, bool shift)
{
    if (row >= 4 || col >= 14) return 0;
    const KeyValue_t& kv = _key_value_map[row][col];
    return shift ? kv.value_second : kv.value_first;
}

}  // namespace

void begin()
{
    // 16 events is ~2 s of fast human typing. Deliberately small: a browser
    // that floods should be dropped at the door, not allowed to build a
    // backlog that replays keys seconds after the user stopped pressing.
    if (!s_q) s_q = xQueueCreate(16, sizeof(Evt));
}

bool post(uint8_t row, uint8_t col, bool shift, bool fn)
{
    if (!s_q || row >= 4 || col >= 14) return false;
    Evt e{Kind::Key, row, col, shift, fn, 0};
    return xQueueSend(s_q, &e, 0) == pdTRUE;   // never block the socket task
}

void update()
{
    if (!s_q) return;
    Evt e;
    // Bounded drain: a full queue must not monopolise a single loop pass and
    // starve the mirror's frame budget.
    for (int i = 0; i < 8 && xQueueReceive(s_q, &e, 0) == pdTRUE; ++i) {
        if (e.kind == Kind::Btn) {
            // Drive the button through M5Unified's own state machine rather than
            // calling a handler directly: setRawState() is what the library uses
            // to feed real GPIO samples, so wasPressed()/wasReleased() and the
            // hold timers all behave exactly as for a physical push. Anything
            // polling M5.BtnA sees no difference between this and a finger.
            const uint32_t now = millis();
            M5.BtnA.setRawState(now, true);
            M5.BtnA.setRawState(now + e.ms, false);
            menu::recordKey(0, 0xFE, 0, false, false, true);
            ++s_applied;
            continue;
        }
        const char c = valueAt(e.row, e.col, e.shift);

        // Log FIRST, unconditionally, before any filtering. A key the menu
        // ignores and a key that never arrived look identical otherwise, and
        // that ambiguity is what made "only enter works" impossible to
        // diagnose. Modifiers are logged too (c == 0 renders as their name).
        menu::recordKey(c, e.row, e.col, e.shift, e.fn, true);
        ++s_applied;

        if (!c) continue;

        // Translate the vendor's control codes into the booleans menu::handleKey
        // expects, and pass printable characters through as a 1-char word --
        // exactly the shape main.cpp builds from a physical press.
        const bool enter = (c == (char)KEY_ENTER);
        const bool del   = (c == (char)KEY_BACKSPACE);
        const bool tab   = (c == (char)KEY_TAB);

        if (enter || del || tab) {
            menu::handleKey(nullptr, 0, enter, del, tab, e.fn);
        } else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7f) {
            menu::handleKey(&c, 1, false, false, false, e.fn);
        } else {
            continue;   // pure modifier (FN/SHIFT/CTRL/ALT/OPT): no menu event
        }
    }
}

void post_trampoline(uint8_t row, uint8_t col, bool shift, bool fn)
{
    if (!post(row, col, shift, fn)) ++s_dropped;
}

bool postBtn(uint8_t btn, uint16_t ms)
{
    if (!s_q || btn != 0) return false;      // only BtnG0 exists to inject
    if (ms < 10)   ms = 10;
    if (ms > 2000) ms = 2000;
    Evt e{Kind::Btn, 0, 0, false, false, ms};
    return xQueueSend(s_q, &e, 0) == pdTRUE;
}

void postBtn_trampoline(uint8_t btn, uint16_t ms)
{
    if (!postBtn(btn, ms)) ++s_dropped;
}

uint32_t applied() { return s_applied; }
uint32_t dropped() { return s_dropped; }

}  // namespace keyinject
