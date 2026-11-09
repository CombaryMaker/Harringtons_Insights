// hd_inflate_fast.h - H-Deflate v1.01 (6 Nov 2026):'s recording inflater with a wide bit buffer and wide match
// copies (the zlib-turbo / libdeflate style refill). Same output and same recorded tokens as hd_inflate.h.
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
// Changes from hd_inflate.h:
//   1. refill: one unaligned 8-byte load tops the 64-bit buffer up to 56+ bits (instead of a byte loop);
//   2. output written through a raw pointer into a buffer with slack (no push_back per byte);
//   3. match copies 8 bytes at a time when the distance is 8 or more (byte loop for short overlapping distances).
#pragma once
#include "hd_inflate.h"
#include <cstring>

namespace hd {

struct FastBits {
    const uint8_t* p; size_t n; size_t pos = 0; uint64_t acc = 0; int nbits = 0;
    FastBits(const uint8_t* d, size_t len) : p(d), n(len) {}
    inline void fill() {
        if (pos + 8 <= n) {
            uint64_t w; memcpy(&w, p + pos, 8);
            acc |= w << nbits;
            pos += (63 - nbits) >> 3;        // bytes actually taken in
            nbits |= 56;                      // now 56..63 valid bits
        } else {
            while (nbits <= 56) { uint64_t b = pos < n ? p[pos] : 0; pos++; acc |= b << nbits; nbits += 8; }
        }
    }
    inline uint32_t peek(int k) { if (nbits < k) fill(); return (uint32_t)(acc & ((1ull << k) - 1)); }
    inline void drop(int k) { acc >>= k; nbits -= k; }
    inline uint32_t get(int k) { if (k == 0) return 0; uint32_t v = peek(k); drop(k); return v; }
    size_t bitpos() const { return pos * 8 - nbits; }
    void align() { int r = nbits & 7; if (r) drop(r); }
    bool past_end() const { return bitpos() > n * 8; }
};

struct HuffDecF : HuffDec {
    inline int decodef(FastBits& br) const {
        uint32_t e = tab[br.peek(maxlen)];
        if (!e) return -1;
        br.drop(e & 15); return (int)(e >> 4);
    }
};

inline bool parse_dyn_header_f(FastBits& br, DynHeader& H) {   // same logic as parse_dyn_header, on FastBits
    H.hlit = (int)br.get(5) + 257; H.hdist = (int)br.get(5) + 1; H.hclen = (int)br.get(4) + 4;
    if (H.hlit > 286 || H.hdist > 30) return false;
    memset(H.bl_len, 0, 19);
    for (int i = 0; i < H.hclen; i++) H.bl_len[bl_order[i]] = (uint8_t)br.get(3);
    HuffDecF bl; if (!bl.build(H.bl_len, 19)) return false;
    H.rle.clear();
    uint8_t lens[320]; int n = 0; int total = H.hlit + H.hdist;
    while (n < total) {
        int s = bl.decodef(br); if (s < 0) return false;
        if (s < 16) { lens[n++] = (uint8_t)s; H.rle.push_back({(uint8_t)s, 0}); }
        else if (s == 16) { if (n == 0) return false; int r = 3 + (int)br.get(2); H.rle.push_back({16, (uint8_t)(r - 3)}); if (n + r > total) return false; uint8_t pv = lens[n - 1]; while (r--) lens[n++] = pv; }
        else if (s == 17) { int r = 3 + (int)br.get(3); H.rle.push_back({17, (uint8_t)(r - 3)}); if (n + r > total) return false; while (r--) lens[n++] = 0; }
        else { int r = 11 + (int)br.get(7); H.rle.push_back({18, (uint8_t)(r - 11)}); if (n + r > total) return false; while (r--) lens[n++] = 0; }
        if (br.past_end()) return false;
    }
    memset(H.llen, 0, 288); memset(H.dlen, 0, 32);
    memcpy(H.llen, lens, H.hlit); memcpy(H.dlen, lens + H.hlit, H.hdist);
    return H.llen[256] != 0;
}

inline bool inflate_record_fast(const uint8_t* data, size_t n, Inflated& R, size_t max_raw = 0) {
    FastBits br(data, n);
    R.raw.clear(); R.tok.clear(); R.blocks.clear(); R.error.clear();
    std::vector<uint8_t> out(std::max<size_t>(n * 4, 1 << 16) + 512); size_t o = 0;   // grows as needed, 512 B slack
    R.tok.reserve(n);
    auto ensure = [&](size_t need) { if (o + need + 512 > out.size()) out.resize((o + need + 512) * 2); };
    HuffDecF L, D;
    for (;;) {
        Block B;
        B.final = br.get(1) != 0; B.type = (int)br.get(2);
        B.tok_begin = R.tok.size(); B.raw_begin = o;
        if (br.past_end()) { R.error = "truncated"; return false; }
        if (B.type == 3) { R.error = "bad block type"; return false; }
        if (B.type == 0) {
            int pad = br.nbits & 7;
            if (pad && br.peek(pad) != 0) { R.error = "nonzero stored padding"; return false; }
            br.align();
            uint32_t len = br.get(16), nlen = br.get(16);
            if ((len ^ 0xffff) != nlen) { R.error = "stored len check"; return false; }
            if (br.past_end()) { R.error = "truncated"; return false; }
            size_t bytepos = br.bitpos() / 8;
            if (bytepos + len > n) { R.error = "truncated stored"; return false; }
            ensure(len); memcpy(out.data() + o, data + bytepos, len); o += len;
            br.acc = 0; br.nbits = 0; br.pos = bytepos + len;
        } else {
            if (B.type == 1) { L.build(stat().llen, 288); D.build(stat().dlen, 32); }
            else {
                B.hdr_bitpos = br.bitpos();
                if (!parse_dyn_header_f(br, B.hdr)) { R.error = "bad dynamic header"; return false; }
                B.hdr_nbits = br.bitpos() - B.hdr_bitpos;
                if (!L.build(B.hdr.llen, 288) || !D.build(B.hdr.dlen, 32)) { R.error = "oversubscribed code"; return false; }
            }
            for (;;) {
                ensure(258);
                int s = L.decodef(br);
                if (s < 0 || br.past_end()) { R.error = "bad code"; return false; }
                if (s < 256) { out[o++] = (uint8_t)s; R.tok.push_back({(uint16_t)s, 0}); continue; }
                if (s == 256) break;
                int ci = s - 257; if (ci > 28) { R.error = "bad length code"; return false; }
                int len = base_length[ci] + (int)br.get(extra_lbits[ci]);
                if (ci == 27 && len == 258) B.alt258.push_back((uint32_t)(R.tok.size() - B.tok_begin));
                int dc = D.decodef(br); if (dc < 0 || dc > 29) { R.error = "bad dist code"; return false; }
                int dist = base_dist[dc] + (int)br.get(extra_dbits[dc]);
                if ((size_t)dist > o) { R.error = "distance too far"; return false; }
                uint8_t* dst = out.data() + o; const uint8_t* src = dst - dist;
                if (dist >= 8) { for (int k = 0; k < len; k += 8) { uint64_t w; memcpy(&w, src + k, 8); memcpy(dst + k, &w, 8); } }
                else { for (int k = 0; k < len; k++) dst[k] = src[k]; }
                o += len;
                R.tok.push_back({(uint16_t)len, (uint32_t)dist});
                if (br.past_end()) { R.error = "truncated"; return false; }
            }
        }
        B.tok_end = R.tok.size(); B.raw_end = o;
        R.blocks.push_back(B);
        if (max_raw && o > max_raw) { R.error = "too big"; return false; }
        if (B.final) break;
    }
    int pad = br.nbits & 7;
    if (pad && br.peek(pad) != 0) { R.error = "nonzero final padding"; return false; }
    br.align();
    if (br.past_end()) { R.error = "truncated"; return false; }
    R.comp_len = br.bitpos() / 8;
    out.resize(o); R.raw.swap(out);
    return true;
}

// v1.01: the fast inflater is the default; inflate_record_ref (hd_inflate.h) is the v1 reference, kept for checks.
inline bool inflate_record(const uint8_t* data, size_t n, Inflated& R, size_t max_raw = 0) { return inflate_record_fast(data, n, R, max_raw); }

} // namespace hd
