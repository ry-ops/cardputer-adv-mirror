/*
 * menu — on-device screens for the Cardputer ADV.
 *
 * WHY THIS EXISTS
 * ---------------
 * Everything this project can do lived in two places the device itself cannot
 * reach: a browser on the far side of a WiFi link, and a serial monitor on the
 * far side of a USB cable. Both are unavailable in exactly the situation you
 * most need them -- when WiFi has not come up.
 *
 * That produced a debugging loop of edit, build, flash, replug, squint at
 * boot output. The Network screen exists to break it: scan, retry, and read
 * the real reason for a failure, on the device, with no host attached.
 *
 * DISPLAY OWNERSHIP
 * -----------------
 * The mirror READS the panel's GRAM; it never writes it. So whatever the menu
 * draws is what the browser mirrors, with no coordination needed between them.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>

namespace menu {

// Draws the initial screen. Call after WiFi and SD are up.
void begin();

// Feed one keyboard event. Returns true if the menu consumed it, in which
// case the caller must not also treat it as mirror test input.
bool handleKey(const char* word, size_t wordLen,
               bool enter, bool del, bool tab, bool fn);

// Record a key event for the Keys test screen. Call for EVERY key, including
// ones handleKey ignores -- that is the entire point: the menu acts on 8 of 56
// keys, so "menu did nothing" must be distinguishable from "key never arrived".
// row/col are hardware matrix coordinates; pass 255 if genuinely unknown.
void recordKey(char ch, uint8_t row, uint8_t col,
               bool shift, bool fn, bool remote);

// True while the Keys test screen is showing. Callers may use this to route
// keys to the log without the menu also acting on them.
bool keysScreenActive();

// Repaint-if-dirty plus periodic refresh of live values. Cheap; call often.
void update();

// True while the menu owns the screen (always, currently -- kept so a future
// full-screen app can take over).
bool active();

}  // namespace menu
