// hd_bits.h - bit reader / writer for deflate (LSB-first). Part of H-Deflate.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See H-Deflate/LICENSE.txt.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <stdexcept>

namespace hd {

struct BitWriter {
    std::vector<uint8_t> out;
    uint64_t acc = 0;   // pending bits, LSB first
    int nbits = 0;
    void put(uint32_t v, int n) {            // n <= 32
        acc |= (uint64_t)v << nbits; nbits += n;
        while (nbits >= 8) { out.push_back((uint8_t)acc); acc >>= 8; nbits -= 8; }
    }
    void align() { if (nbits) { out.push_back((uint8_t)acc); acc = 0; nbits = 0; } }
    void put_rev(uint32_t code, int len) {   // huffman codes are sent MSB-first: reverse then put
        uint32_t r = 0; for (int i = 0; i < len; i++) { r = (r << 1) | (code & 1); code >>= 1; }
        put(r, len);
    }
    size_t bitpos() const { return out.size() * 8 + nbits; }
    void finish() { align(); }
};

struct BitReader {
    const uint8_t* p; size_t n; size_t pos = 0;   // byte position
    uint64_t acc = 0; int nbits = 0;
    bool overrun = false;
    BitReader(const uint8_t* d, size_t len) : p(d), n(len) {}
    inline void fill() {   // bytes beyond the end read as zero; pos keeps counting so bitpos() stays exact
        while (nbits <= 56) { uint64_t b = pos < n ? p[pos] : 0; pos++; acc |= b << nbits; nbits += 8; }
    }
    inline uint32_t peek(int k) { if (nbits < k) fill(); return (uint32_t)(acc & ((1ull << k) - 1)); }
    inline void drop(int k) { acc >>= k; nbits -= k; }
    inline uint32_t get(int k) { if (k == 0) return 0; uint32_t v = peek(k); drop(k); return v; }
    // bits consumed so far (including bits buffered beyond the stream end that were never used)
    size_t bitpos() const { return pos * 8 - nbits; }
    void align() { int r = nbits & 7; if (r) drop(r); }
    bool at_byte() const { return (nbits & 7) == 0; }
    bool past_end() const { return bitpos() > n * 8; }
};

// variable-length integers for the reconstruction record
inline void put_var(std::vector<uint8_t>& v, uint64_t x) { while (x >= 128) { v.push_back((uint8_t)(x | 128)); x >>= 7; } v.push_back((uint8_t)x); }
struct VarReader {
    const uint8_t* p; size_t n; size_t i = 0;
    VarReader(const uint8_t* d, size_t len) : p(d), n(len) {}
    uint64_t get() { uint64_t x = 0; int sh = 0; for (;;) { if (i >= n) throw std::runtime_error("record truncated"); uint8_t b = p[i++]; x |= (uint64_t)(b & 127) << sh; if (!(b & 128)) break; sh += 7; } return x; }
    uint8_t byte() { if (i >= n) throw std::runtime_error("record truncated"); return p[i++]; }
    bool done() const { return i >= n; }
};

} // namespace hd
