/*
 * keyinject — remote key events from the browser, applied on the loop task.
 *
 * WHY A QUEUE AND NOT A DIRECT CALL
 * ---------------------------------
 * The WebSocket handler runs on the AsyncTCP task. menu::handleKey() touches
 * the display and menu state, which the loop task also touches every frame.
 * Calling it from the socket callback would be a data race on menu state and a
 * concurrent SPI transaction on the panel -- the same class of defect that
 * forced SD formatting off the async task (ADR 0008).
 *
 * So the socket task only enqueues. loop() drains. The queue is a FreeRTOS
 * queue because it is the one primitive here that is genuinely ISR/task-safe
 * without hand-rolled critical sections.
 *
 * WHAT A KEY IS
 * -------------
 * The ADV keyboard is a 4x14 matrix read over I2C (TCA8418). M5Cardputer maps
 * a (row, col) coordinate through _key_value_map[row][col] to a character.
 * Rather than invent a second vocabulary for the wire, the browser sends the
 * SAME (row, col) the hardware would have produced. That means:
 *
 *   - the web keyboard cannot drift from the hardware keymap, because it IS
 *     the hardware keymap, extracted from the vendor header at build time;
 *   - a remote press and a physical press converge on identical code paths,
 *     so there is no second input path to debug.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace keyinject {

// Call once from setup(), before the web server starts.
void begin();

// Enqueue a matrix coordinate. SAFE FROM ANY TASK. Returns false if the queue
// is full (browser flooding) or the coordinate is out of range.
bool post(uint8_t row, uint8_t col, bool shift, bool fn);

// void-returning adapter matching CardputerMirror::KeyFn. The mirror's sink
// cannot report backpressure, so a drop is counted here rather than silently
// discarded -- see dropped().
void post_trampoline(uint8_t row, uint8_t col, bool shift, bool fn);

// Events refused because the queue was full. A nonzero value means the browser
// is sending faster than loop() drains, which is a real symptom, not noise.
uint32_t dropped();

// Enqueue a top-edge button press. SAFE FROM ANY TASK. btn 0 == BtnG0 (GPIO 0),
// the only top button M5Unified registers for board_M5CardputerADV. BtnRst is
// absent on purpose: it drives EN and cuts power to the SoC, so no software
// press exists to simulate.
bool postBtn(uint8_t btn, uint16_t ms);
void postBtn_trampoline(uint8_t btn, uint16_t ms);

// Drain and apply queued events. Call from loop() ONLY.
void update();

// Total events applied since boot -- proves the path end to end.
uint32_t applied();

}  // namespace keyinject
