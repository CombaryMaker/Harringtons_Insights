// x86f.h - E8/E9 (x86 CALL/JMP) address filter, written for HX Complete, 3 Nov 2026.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See HX/LICENSE.txt.
// The idea is the well-known BCJ filter (relative call targets made absolute so repeats line up); this version
// is our own simple invertible form: an E8/E9 byte whose 32-bit offset has a top byte of 00 or FF has the offset
// replaced by (offset + position) wrapped to 25 bits (top byte again 00 or FF). An E8/E9 that is left alone
// blocks conversions in the next 4 bytes, so the decoder sees exactly the bytes the encoder looked at.
#pragma once
#include <cstdint>
#include <cstddef>
static inline size_t x86_filter(uint8_t *b, size_t n, bool enc) {   // returns the number of addresses converted
    size_t cnt = 0; if (n < 5) return 0; long long last = -100;
    for (size_t i = 0; i + 4 < n;) {
        if ((b[i] & 0xFE) != 0xE8) { i++; continue; }
        bool blocked = (long long)i - last <= 4; uint8_t t = b[i + 4];
        if (blocked || !(t == 0x00 || t == 0xFF)) { last = (long long)i; i++; continue; }
        int32_t a = (int32_t)(b[i + 1] | (b[i + 2] << 8) | (b[i + 3] << 16) | ((uint32_t)b[i + 4] << 24));
        int64_t v = enc ? (int64_t)a + (int64_t)i : (int64_t)a - (int64_t)i;
        uint32_t w = (uint32_t)v & 0x01FFFFFF; if (w & 0x01000000) w |= 0xFE000000;   // wrap to 25 bits, sign-extended
        b[i + 1] = w & 255; b[i + 2] = (w >> 8) & 255; b[i + 3] = (w >> 16) & 255; b[i + 4] = (w >> 24) & 255; i += 5; cnt++;
    }
    return cnt;
}
