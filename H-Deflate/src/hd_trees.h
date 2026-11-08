// hd_trees.h - deflate symbol tables, Huffman code building (zlib-family tie rules), block header coding.
// Part of H-Deflate. Written from RFC 1951 and the published description of the zlib/gzip tree builder.
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#pragma once
#include "hd_bits.h"
#include <cstring>
#include <algorithm>

namespace hd {

enum { LITERALS = 256, END_BLOCK = 256, L_CODES = 286, D_CODES = 30, BL_CODES = 19, MAX_BITS = 15, MAX_BL_BITS = 7,
       MIN_MATCH = 3, MAX_MATCH = 258, REP_3_6 = 16, REPZ_3_10 = 17, REPZ_11_138 = 18 };

static const int extra_lbits[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int extra_dbits[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const int extra_blbits[19] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,7};
static const uint8_t bl_order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
static const int base_length[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int base_dist[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

struct Tables {
    uint8_t length_code[256];   // match length - 3 -> length symbol index (0..28)
    uint8_t dist_code[512];     // distance -> dist code (first 256 for dist-1 < 256, then (dist-1)>>7 + 256)
    Tables() {
        int length = 0;
        for (int code = 0; code < 28; code++) for (int n = 0; n < (1 << extra_lbits[code]); n++) length_code[length++] = code;
        length_code[255] = 28;   // 258 (base 258 = code 285 -> index 28); overrides the last slot of code 27
        int dist = 0;
        for (int code = 0; code < 16; code++) for (int n = 0; n < (1 << extra_dbits[code]); n++) dist_code[dist++] = code;
        dist >>= 7;
        for (int code = 16; code < 30; code++) for (int n = 0; n < (1 << (extra_dbits[code] - 7)); n++) dist_code[256 + dist++] = code;
    }
    inline int lcode(int len) const { return length_code[len - 3]; }            // 0..28 -> symbol 257+
    inline int dcode(int dist) const { return dist <= 256 ? dist_code[dist - 1] : dist_code[256 + ((dist - 1) >> 7)]; }
};
inline const Tables& tables() { static Tables t; return t; }

// Static (fixed) code lengths
struct StaticTrees {
    uint8_t llen[288]; uint8_t dlen[32];
    StaticTrees() {
        int n = 0; while (n <= 143) llen[n++] = 8; while (n <= 255) llen[n++] = 9; while (n <= 279) llen[n++] = 7; while (n <= 287) llen[n++] = 8;
        for (int i = 0; i < 32; i++) dlen[i] = 5;
    }
};
inline const StaticTrees& stat() { static StaticTrees s; return s; }

// canonical codes from lengths (bit-reversed ready for LSB-first output)
inline void gen_codes(const uint8_t* len, int n, uint16_t* code) {
    int bl_count[MAX_BITS + 1] = {0};
    for (int i = 0; i < n; i++) bl_count[len[i]]++;
    bl_count[0] = 0;
    uint32_t next_code[MAX_BITS + 2]; uint32_t c = 0;
    for (int bits = 1; bits <= MAX_BITS; bits++) { c = (c + bl_count[bits - 1]) << 1; next_code[bits] = c; }
    for (int i = 0; i < n; i++) { int l = len[i]; if (!l) { code[i] = 0; continue; } uint32_t v = next_code[l]++; uint32_t r = 0; for (int k = 0; k < l; k++) { r = (r << 1) | (v & 1); v >>= 1; } code[i] = (uint16_t)r; }
}

// Huffman code-length builder with the zlib-family tie rules (heap ordered by frequency then node depth,
// length limit enforced by moving leaves down, lengths assigned in heap order).
// Returns code lengths in len[]; opt_len/static_len accumulate the bit cost of the block like zlib does.
struct TreeBuild {
    int freq[2 * L_CODES + 1]; int dad[2 * L_CODES + 1]; uint8_t depth[2 * L_CODES + 1]; int heap[2 * L_CODES + 1];
    int heap_len, heap_max; int max_code;
    uint64_t opt_len = 0, static_len = 0;
    uint8_t len[2 * L_CODES + 1];

    inline bool smaller(int n, int m) const { return freq[n] < freq[m] || (freq[n] == freq[m] && depth[n] <= depth[m]); }
    void downheap(int k) {
        int v = heap[k]; int j = k << 1;
        while (j <= heap_len) {
            if (j < heap_len && smaller(heap[j + 1], heap[j])) j++;
            if (smaller(v, heap[j])) break;
            heap[k] = heap[j]; k = j; j <<= 1;
        }
        heap[k] = v;
    }
    // build for `elems` symbols with frequencies f[], extra bits xb[] (indexed by symbol - base for len tree),
    // static lengths slen (or null), max bit length
    void build(const int* f, int elems, const uint8_t* slen, int max_length, int (*xbits)(int)) {
        const int HEAP_SIZE = 2 * L_CODES + 1;
        for (int n = 0; n < elems; n++) { freq[n] = f[n]; len[n] = 0; }
        heap_len = 0; heap_max = HEAP_SIZE; max_code = -1;
        for (int n = 0; n < elems; n++) { if (freq[n] != 0) { heap[++heap_len] = max_code = n; depth[n] = 0; } else len[n] = 0; }
        while (heap_len < 2) {
            int node = heap[++heap_len] = (max_code < 2 ? ++max_code : 0);
            freq[node] = 1; depth[node] = 0; opt_len--; if (slen) static_len -= slen[node];
        }
        for (int n = heap_len / 2; n >= 1; n--) downheap(n);
        int node = elems;
        do {
            int n = heap[1]; heap[1] = heap[heap_len--]; downheap(1);
            int m = heap[1];
            heap[--heap_max] = n; heap[--heap_max] = m;
            freq[node] = freq[n] + freq[m];
            depth[node] = (uint8_t)((depth[n] >= depth[m] ? depth[n] : depth[m]) + 1);
            dad[n] = dad[m] = node;
            heap[1] = node++; downheap(1);
        } while (heap_len >= 2);
        heap[--heap_max] = heap[1];
        // gen_bitlen
        int bl_count[MAX_BITS + 1] = {0};
        int overflow = 0;
        len[heap[heap_max]] = 0;
        int h;
        for (h = heap_max + 1; h < HEAP_SIZE; h++) {
            int n = heap[h];
            int bits = len[dad[n]] + 1;
            if (bits > max_length) { bits = max_length; overflow++; }
            len[n] = (uint8_t)bits;
            if (n > max_code) continue;   // internal node
            bl_count[bits]++;
            int xb = n >= 0 ? xbits(n) : 0;
            int ff = freq[n];
            opt_len += (uint64_t)ff * (bits + xb);
            if (slen) static_len += (uint64_t)ff * (slen[n] + xb);
        }
        if (overflow) {
            do {
                int bits = max_length - 1;
                while (bl_count[bits] == 0) bits--;
                bl_count[bits]--; bl_count[bits + 1] += 2; bl_count[max_length]--;
                overflow -= 2;
            } while (overflow > 0);
            for (int bits = max_length; bits != 0; bits--) {
                int n = bl_count[bits];
                while (n != 0) {
                    int m = heap[--h];
                    if (m > max_code) continue;
                    if (len[m] != bits) { opt_len += (uint64_t)((int)bits - (int)len[m]) * freq[m]; len[m] = (uint8_t)bits; }
                    n--;
                }
            }
        }
    }
};

inline int xb_len(int n) { return n >= 257 ? extra_lbits[n - 257] : 0; }
inline int xb_dist(int n) { return extra_dbits[n]; }
inline int xb_bl(int n) { return extra_blbits[n]; }

// Run-length scan of code lengths the zlib way: counts into bl frequencies (emit=false) or writes codes (emit=true)
struct BlScan {
    int bl_freq[BL_CODES];
    void scan(const uint8_t* tree, int max_code) {
        int prevlen = -1, nextlen = tree[0], count = 0, max_count = 7, min_count = 4;
        if (nextlen == 0) { max_count = 138; min_count = 3; }
        for (int n = 0; n <= max_code; n++) {
            int curlen = nextlen; nextlen = (n + 1 <= max_code) ? tree[n + 1] : 0xffff;
            if (++count < max_count && curlen == nextlen) continue;
            else if (count < min_count) bl_freq[curlen] += count;
            else if (curlen != 0) { if (curlen != prevlen) bl_freq[curlen]++; bl_freq[REP_3_6]++; }
            else if (count <= 10) bl_freq[REPZ_3_10]++;
            else bl_freq[REPZ_11_138]++;
            count = 0; prevlen = curlen;
            if (nextlen == 0) { max_count = 138; min_count = 3; }
            else if (curlen == nextlen) { max_count = 6; min_count = 3; }
            else { max_count = 7; min_count = 4; }
        }
    }
    // emit the same sequence as symbols (sym, extra value) pairs
    template<class F> static void emit(const uint8_t* tree, int max_code, F&& put) {
        int prevlen = -1, nextlen = tree[0], count = 0, max_count = 7, min_count = 4;
        if (nextlen == 0) { max_count = 138; min_count = 3; }
        for (int n = 0; n <= max_code; n++) {
            int curlen = nextlen; nextlen = (n + 1 <= max_code) ? tree[n + 1] : 0xffff;
            if (++count < max_count && curlen == nextlen) continue;
            else if (count < min_count) { do { put(curlen, 0); } while (--count != 0); }
            else if (curlen != 0) { if (curlen != prevlen) { put(curlen, 0); count--; } put(REP_3_6, count - 3); }
            else if (count <= 10) put(REPZ_3_10, count - 3);
            else put(REPZ_11_138, count - 11);
            count = 0; prevlen = curlen;
            if (nextlen == 0) { max_count = 138; min_count = 3; }
            else if (curlen == nextlen) { max_count = 6; min_count = 3; }
            else { max_count = 7; min_count = 4; }
        }
    }
};

// A token: literal (dist == 0, len = byte) or match (len 3..258, dist 1..32768). alt258: length 258 coded as 284+31.
struct Token { uint16_t len; uint32_t dist; };

// Full description of one deflate block's header as actually written (for exact re-emission)
struct DynHeader {
    int hlit = 0, hdist = 0, hclen = 0;             // counts as stored in the stream (257+, 1+, 4+)
    uint8_t bl_len[19] = {0};                        // code-length-code lengths in bl_order order positions 0..hclen-1 (stored in natural index order here)
    std::vector<std::pair<uint8_t, uint8_t>> rle;    // (symbol 0..18, extra value) sequence as written
    uint8_t llen[288] = {0}; uint8_t dlen[32] = {0}; // resulting code lengths
    bool operator==(const DynHeader& o) const {
        return hlit == o.hlit && hdist == o.hdist && hclen == o.hclen && !memcmp(bl_len, o.bl_len, 19) && rle == o.rle;
    }
};

// Plan for a block as zlib would write it, from the block's tokens: type decision and (for dynamic) the header.
struct BlockPlan {
    int type = 0;          // 0 stored, 1 static, 2 dynamic
    DynHeader hdr;         // valid when type == 2
    uint8_t llen[288]; uint8_t dlen[32];   // lengths used for coding (static or dynamic)
};

inline void count_freqs(const Token* t, size_t n, int* lf, int* df) {
    memset(lf, 0, sizeof(int) * L_CODES); memset(df, 0, sizeof(int) * D_CODES);
    const Tables& T = tables();
    for (size_t i = 0; i < n; i++) {
        if (t[i].dist == 0) lf[t[i].len]++;
        else { lf[257 + T.lcode(t[i].len)]++; df[T.dcode(t[i].dist)]++; }
    }
    lf[END_BLOCK]++;
}

// zlib-style decision for a block of tokens covering stored_len raw bytes. can_store: raw bytes still available.
// strategy_fixed: force static trees. level0: stored only.
inline BlockPlan plan_block(const Token* t, size_t n, size_t stored_len, bool can_store, bool strategy_fixed) {
    BlockPlan P;
    int lf[L_CODES], df[D_CODES];
    count_freqs(t, n, lf, df);
    TreeBuild L, D, B;
    L.build(lf, L_CODES, stat().llen, MAX_BITS, xb_len);
    D.opt_len = L.opt_len; D.static_len = L.static_len;
    D.build(df, D_CODES, stat().dlen, MAX_BITS, xb_dist);
    // bl tree
    BlScan S; memset(S.bl_freq, 0, sizeof(S.bl_freq));
    S.scan(L.len, L.max_code); S.scan(D.len, D.max_code);
    B.opt_len = D.opt_len; B.static_len = D.static_len;
    B.build(S.bl_freq, BL_CODES, nullptr, MAX_BL_BITS, xb_bl);
    int max_blindex;
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--) if (B.len[bl_order[max_blindex]] != 0) break;
    uint64_t opt_len = B.opt_len + 3 * ((uint64_t)max_blindex + 1) + 5 + 5 + 4;
    uint64_t static_len = B.static_len;
    uint64_t opt_lenb = (opt_len + 3 + 7) >> 3, static_lenb = (static_len + 3 + 7) >> 3;
    if (static_lenb <= opt_lenb || strategy_fixed) opt_lenb = static_lenb;
    if (stored_len + 4 <= opt_lenb && can_store && stored_len <= 65535) { P.type = 0; return P; }
    if (static_lenb == opt_lenb) { P.type = 1; memcpy(P.llen, stat().llen, 288); memcpy(P.dlen, stat().dlen, 32); return P; }
    P.type = 2;
    DynHeader& H = P.hdr;
    H.hlit = L.max_code + 1; H.hdist = D.max_code + 1; H.hclen = max_blindex + 1;
    for (int i = 0; i < 19; i++) H.bl_len[i] = B.len[i];
    BlScan::emit(L.len, L.max_code, [&](int s, int x) { H.rle.push_back({(uint8_t)s, (uint8_t)x}); });
    BlScan::emit(D.len, D.max_code, [&](int s, int x) { H.rle.push_back({(uint8_t)s, (uint8_t)x}); });
    memset(H.llen, 0, 288); memset(H.dlen, 0, 32);
    for (int i = 0; i < H.hlit; i++) H.llen[i] = L.len[i];
    for (int i = 0; i < H.hdist; i++) H.dlen[i] = D.len[i];
    memcpy(P.llen, H.llen, 288); memcpy(P.dlen, H.dlen, 32);
    return P;
}

// write a dynamic header exactly as described
inline void write_dyn_header(BitWriter& w, const DynHeader& H) {
    w.put(H.hlit - 257, 5); w.put(H.hdist - 1, 5); w.put(H.hclen - 4, 4);
    for (int i = 0; i < H.hclen; i++) w.put(H.bl_len[bl_order[i]], 3);
    uint16_t blcode[19]; gen_codes(H.bl_len, 19, blcode);
    for (auto& e : H.rle) {
        w.put(blcode[e.first], H.bl_len[e.first]);
        if (e.first >= 16) w.put(e.second, extra_blbits[e.first]);
    }
}

// write tokens with given code lengths, then end-of-block
inline void write_tokens(BitWriter& w, const Token* t, size_t n, const uint8_t* llen, const uint8_t* dlen, const std::vector<uint32_t>* alt258 = nullptr) {
    uint16_t lc[288], dc[32]; gen_codes(llen, 288, lc); gen_codes(dlen, 32, dc);
    const Tables& T = tables();
    size_t ai = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i].dist == 0) { w.put(lc[t[i].len], llen[t[i].len]); continue; }
        int len = t[i].len; int code = T.lcode(len);
        if (alt258 && ai < alt258->size() && (*alt258)[ai] == i) { ai++; code = 27; }   // 258 written as 284 + 31
        w.put(lc[257 + code], llen[257 + code]);
        if (extra_lbits[code]) w.put(len - base_length[code], extra_lbits[code]);
        int d = t[i].dist; int dcd = T.dcode(d);
        w.put(dc[dcd], dlen[dcd]);
        if (extra_dbits[dcd]) w.put(d - base_dist[dcd], extra_dbits[dcd]);
    }
    w.put(lc[END_BLOCK], llen[END_BLOCK]);
}

} // namespace hd
