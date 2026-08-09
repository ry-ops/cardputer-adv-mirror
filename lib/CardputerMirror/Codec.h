/*
 * Tile codec — RLE16 over RGB565.
 *
 * Cardputer UIs are dominated by flat fills and text, so run-length coding
 * beats generic compression at a fraction of the CPU and RAM. Worst case
 * (pure noise) it degrades to 3 B per 1 px run, so we fall back to RAW
 * whenever the encoded size is not a win.
 *
 * Wire format, little-endian:
 *   RAW: [0x00][u16 count][count * u16 pixel]
 *   RLE: [0x01][u16 runs ][runs  * (u16 pixel, u8 runlen 1..255)]
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace cmirror {

enum : uint8_t { kEncRaw = 0x00, kEncRle = 0x01 };

// Encode `count` RGB565 pixels into out (capacity outCap). Returns bytes
// written, or 0 if it does not fit. Chooses RAW vs RLE automatically.
inline size_t encodeTile(const uint16_t* px, size_t count,
                         uint8_t* out, size_t outCap)
{
    const size_t rawSize = 3 + count * 2;

    // Measure RLE first so we can pick the smaller encoding.
    size_t runs = 0;
    for (size_t i = 0; i < count; ) {
        uint16_t v = px[i];
        size_t j = i + 1;
        while (j < count && px[j] == v && (j - i) < 255) ++j;
        ++runs;
        i = j;
    }
    const size_t rleSize = 3 + runs * 3;

    if (rleSize >= rawSize) {
        if (outCap < rawSize) return 0;
        out[0] = kEncRaw;
        out[1] = (uint8_t)(count & 0xFF);
        out[2] = (uint8_t)(count >> 8);
        // memcpy is safe: RGB565 little-endian on both ESP32-S3 and the browser.
        for (size_t i = 0; i < count; ++i) {
            out[3 + i * 2]     = (uint8_t)(px[i] & 0xFF);
            out[3 + i * 2 + 1] = (uint8_t)(px[i] >> 8);
        }
        return rawSize;
    }

    if (outCap < rleSize) return 0;
    out[0] = kEncRle;
    out[1] = (uint8_t)(runs & 0xFF);
    out[2] = (uint8_t)(runs >> 8);
    size_t o = 3;
    for (size_t i = 0; i < count; ) {
        uint16_t v = px[i];
        size_t j = i + 1;
        while (j < count && px[j] == v && (j - i) < 255) ++j;
        out[o++] = (uint8_t)(v & 0xFF);
        out[o++] = (uint8_t)(v >> 8);
        out[o++] = (uint8_t)(j - i);
        i = j;
    }
    return o;
}

// CRC32 (IEEE, reflected) — change detection only, never integrity.
inline uint32_t crc32(const void* data, size_t len, uint32_t crc = 0xFFFFFFFFu)
{
    const uint8_t* p = (const uint8_t*)data;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

}  // namespace cmirror
