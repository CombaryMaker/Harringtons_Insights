// hd_inflate.h - recording inflater: decodes a raw deflate stream and keeps every token, block and header detail
// needed to re-emit the stream bit for bit. Written from RFC 1951. Part of H-Deflate.
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#pragma once
#include "hd_trees.h"

namespace hd {

struct HuffDec {
    int maxlen = 0;
    std::vector<uint32_t> tab;   // index: next `maxlen` bits (LSB-first) -> (sym << 4) | len ; 0 = invalid
    bool build(const uint8_t* len, int n) {
        maxlen = 0; for (int i = 0; i < n; i++) if (len[i] > maxlen) maxlen = len[i];
        if (maxlen == 0) { tab.assign(1, 0); return true; }
        tab.assign(1u << maxlen, 0);
        uint16_t code[320]; gen_codes(len, n, code);
        // check for over-subscription
        long left = 1; int cnt[16] = {0}; for (int i = 0; i < n; i++) cnt[len[i]]++;
        for (int l = 1; l <= 15; l++) { left <<= 1; left -= cnt[l]; if (left < 0) return false; }
        for (int i = 0; i < n; i++) {
            int l = len[i]; if (!l) continue;
            uint32_t c = code[i];   // already bit-reversed, l bits
            for (uint32_t k = c; k < tab.size(); k += 1u << l) tab[k] = ((uint32_t)i << 4) | l;
        }
        return true;
    }
    inline int decode(BitReader& br) const {
        uint32_t e = tab[br.peek(maxlen)];
        if (!e) return -1;
        br.drop(e & 15); return (int)(e >> 4);
    }
};

struct Block {
    int type = 0;            // 0 stored, 1 static, 2 dynamic
    bool final = false;
    size_t tok_begin = 0, tok_end = 0;   // token range (dynamic/static)
    size_t raw_begin = 0, raw_end = 0;   // raw byte range covered
    DynHeader hdr;           // type 2
    size_t hdr_bitpos = 0, hdr_nbits = 0;  // position/length of the header bits in the stream (after the 3 block bits)
    std::vector<uint32_t> alt258;        // token indices (relative to tok_begin) coded as 284+31
};

struct Inflated {
    std::vector<uint8_t> raw;
    std::vector<Token> tok;
    std::vector<Block> blocks;
    size_t comp_len = 0;     // bytes of deflate data consumed (ends at a byte boundary after the final block)
    std::string error;
};

// parse a dynamic block header (after the 3 block-header bits). Returns false on error.
inline bool parse_dyn_header(BitReader& br, DynHeader& H) {
    H.hlit = (int)br.get(5) + 257; H.hdist = (int)br.get(5) + 1; H.hclen = (int)br.get(4) + 4;
    if (H.hlit > 286 || H.hdist > 30) return false;
    memset(H.bl_len, 0, 19);
    for (int i = 0; i < H.hclen; i++) H.bl_len[bl_order[i]] = (uint8_t)br.get(3);
    HuffDec bl; if (!bl.build(H.bl_len, 19)) return false;
    H.rle.clear();
    uint8_t lens[320]; int n = 0; int total = H.hlit + H.hdist;
    while (n < total) {
        int s = bl.decode(br); if (s < 0) return false;
        if (s < 16) { lens[n++] = (uint8_t)s; H.rle.push_back({(uint8_t)s, 0}); }
        else if (s == 16) { if (n == 0) return false; int r = 3 + (int)br.get(2); H.rle.push_back({16, (uint8_t)(r - 3)}); if (n + r > total) return false; uint8_t p = lens[n - 1]; while (r--) lens[n++] = p; }
        else if (s == 17) { int r = 3 + (int)br.get(3); H.rle.push_back({17, (uint8_t)(r - 3)}); if (n + r > total) return false; while (r--) lens[n++] = 0; }
        else { int r = 11 + (int)br.get(7); H.rle.push_back({18, (uint8_t)(r - 11)}); if (n + r > total) return false; while (r--) lens[n++] = 0; }
        if (br.past_end()) return false;
    }
    memset(H.llen, 0, 288); memset(H.dlen, 0, 32);
    memcpy(H.llen, lens, H.hlit); memcpy(H.dlen, lens + H.hlit, H.hdist);
    if (H.llen[256] == 0) return false;
    return true;
}

// Inflate a raw deflate stream starting at data[0..n). Records everything. max_raw caps output (0 = no cap).
inline bool inflate_record_ref(const uint8_t* data, size_t n, Inflated& R, size_t max_raw = 0) {
    BitReader br(data, n);
    R.raw.clear(); R.tok.clear(); R.blocks.clear(); R.error.clear();
    const Tables& T = tables();
    HuffDec L, D;
    for (;;) {
        Block B;
        B.final = br.get(1) != 0; B.type = (int)br.get(2);
        B.tok_begin = R.tok.size(); B.raw_begin = R.raw.size();
        if (br.past_end()) { R.error = "truncated"; return false; }
        if (B.type == 3) { R.error = "bad block type"; return false; }
        if (B.type == 0) {
            int pad = br.nbits & 7;
            if (pad && br.peek(pad) != 0) { R.error = "nonzero stored padding"; return false; }
            br.align();
            uint32_t len = br.get(16), nlen = br.get(16);
            if ((len ^ 0xffff) != nlen) { R.error = "stored len check"; return false; }
            if (br.past_end()) { R.error = "truncated"; return false; }
            // bit reader is byte aligned now; copy from its byte position minus buffered bytes
            size_t bytepos = br.bitpos() / 8;
            if (bytepos + len > n) { R.error = "truncated stored"; return false; }
            R.raw.insert(R.raw.end(), data + bytepos, data + bytepos + len);
            // reposition reader
            br.acc = 0; br.nbits = 0; br.pos = bytepos + len;
        } else {
            if (B.type == 1) { L.build(stat().llen, 288); D.build(stat().dlen, 32); }
            else {
                B.hdr_bitpos = br.bitpos();
                if (!parse_dyn_header(br, B.hdr)) { R.error = "bad dynamic header"; return false; }
                B.hdr_nbits = br.bitpos() - B.hdr_bitpos;
                if (!L.build(B.hdr.llen, 288) || !D.build(B.hdr.dlen, 32)) { R.error = "oversubscribed code"; return false; }
            }
            for (;;) {
                int s = L.decode(br);
                if (s < 0 || br.past_end()) { R.error = "bad code"; return false; }
                if (s < 256) { R.raw.push_back((uint8_t)s); R.tok.push_back({(uint16_t)s, 0}); continue; }
                if (s == 256) break;
                int ci = s - 257; if (ci > 28) { R.error = "bad length code"; return false; }
                int len = base_length[ci] + (int)br.get(extra_lbits[ci]);
                if (ci == 27 && len == 258) B.alt258.push_back((uint32_t)(R.tok.size() - B.tok_begin));
                int dc = D.decode(br); if (dc < 0 || dc > 29) { R.error = "bad dist code"; return false; }
                int dist = base_dist[dc] + (int)br.get(extra_dbits[dc]);
                if ((size_t)dist > R.raw.size()) { R.error = "distance too far"; return false; }
                size_t p = R.raw.size() - dist;
                for (int k = 0; k < len; k++) R.raw.push_back(R.raw[p + k]);
                R.tok.push_back({(uint16_t)len, (uint32_t)dist});
                if (br.past_end()) { R.error = "truncated"; return false; }
            }
        }
        B.tok_end = R.tok.size(); B.raw_end = R.raw.size();
        R.blocks.push_back(B);
        if (max_raw && R.raw.size() > max_raw) { R.error = "too big"; return false; }
        if (B.final) break;
    }
    // final padding must be zero and the stream must end on a byte boundary inside the data
    int pad = br.nbits & 7;
    if (pad && br.peek(pad) != 0) { R.error = "nonzero final padding"; return false; }
    br.align();
    if (br.past_end()) { R.error = "truncated"; return false; }
    R.comp_len = br.bitpos() / 8;
    return true;
}

} // namespace hd
