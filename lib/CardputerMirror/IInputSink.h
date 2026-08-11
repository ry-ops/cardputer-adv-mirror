/*
 * IInputSink — ADR 0038. Core-owned interface; firmwares implement it.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>

namespace cmirror {

// Wire vocabulary: hardware matrix coordinates, the same (row, col) a
// physical TCA8418 press would report. See src/keyinject.h for why the
// browser sends this instead of a character.
struct RemoteKey {
    uint8_t row;
    uint8_t col;
    bool    shift;
    bool    fn;
};

// inject()/injectBtn() are called from the AsyncTCP task (see the WebSocket
// handler in CardputerMirror.cpp). MUST be non-blocking and MUST NOT touch
// the display -- enqueue only. Same contract the legacy onKey()/onBtn()
// callbacks already document; this interface just gives it a name and a home
// on the adapter instead of a loose function pointer.
class IInputSink {
public:
    virtual ~IInputSink() = default;
    virtual void begin() = 0;
    virtual void inject(const RemoteKey& k) = 0;
    virtual void injectBtn(uint8_t btn, uint16_t ms) {}
    virtual void poll() {}
};

}  // namespace cmirror
