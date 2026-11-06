// hd_match.h - H-Deflate matcher: a lazy hash-chain LZ77 encoder with the decision rules of the zlib family,
// parametrised beyond zlib's table, usable as a predictor (predict / commit) for reconstruction.
// Written from RFC 1951 and the published description of the method (gzip/zlib "algorithm" notes).
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See H-Deflate/LICENSE.txt.
#pragma once
#include "hd_trees.h"
#include <memory>

namespace hd {

struct Params {
    int wbits = 15;        // window 2^wbits (9..15)
    int hash_bits = 15;    // zlib: memLevel + 7
    int mem_log = 8;       // zlib memLevel (block symbol limit 2^(mem_log+6) - 1)
    int good = 32, lazy = 258, nice = 258, chain = 4096;
    bool fast = false;     // levels 1-3 style (no lazy evaluation)
    bool filtered = false; // Z_FILTERED
    bool fixed = false;    // Z_FIXED (static trees only)
    bool rle = false;      // Z_RLE
    bool huff = false;     // Z_HUFFMAN_ONLY
    bool stored = false;   // level 0
    bool too_far = true;   // drop 3-byte matches farther than 4096 (zlib rule in slow mode)
    bool stale = true;     // emulate zlib's stale window bytes beyond the end of input after a slide
    int level_hint = 6;    // only used to form a zlib header FLEVEL
    std::string name() const {
        char b[128]; snprintf(b, sizeof b, "w%d h%d m%d g%d l%d n%d c%d%s%s%s%s%s%s", wbits, hash_bits, mem_log, good, lazy, nice, chain,
                              fast ? " fast" : "", filtered ? " filt" : "", fixed ? " fixed" : "", rle ? " rle" : "", huff ? " huff" : "", stored ? " stored" : "");
        return b;
    }
};

// zlib's configuration table for levels 1..9 (good, lazy, nice, chain, fast?)
inline Params zlib_params(int level, int mem_level = 8, int wbits = 15, int strategy = 0) {
    static const int tab[10][4] = {{0,0,0,0},{4,4,8,4},{4,5,16,8},{4,6,32,32},{4,4,16,16},{8,16,32,32},{8,16,128,128},{8,32,128,256},{32,128,258,1024},{32,258,258,4096}};
    Params p; p.wbits = wbits; p.hash_bits = mem_level + 7; p.mem_log = mem_level; p.level_hint = level;
    p.good = tab[level][0]; p.lazy = tab[level][1]; p.nice = tab[level][2]; p.chain = tab[level][3];
    p.fast = level >= 1 && level <= 3; p.stored = level == 0;
    p.filtered = strategy == 1; p.huff = strategy == 2; p.rle = strategy == 3; p.fixed = strategy == 4;
    return p;
}

class Matcher {
public:
    Params P;
    const uint8_t* in = nullptr; size_t N = 0;
    std::vector<uint8_t> buf;      // input + 258 bytes beyond-end emulation
    std::vector<uint32_t> head, prev;
    uint32_t wsize, wmask, hmask; int hshift; uint32_t max_dist;
    size_t max_insert;             // last position that may be inserted into the hash (N - 3)
    uint32_t strstart = 0, ins_h = 0;
    uint32_t match_length = 2, match_start = 0, prev_length = 2, prev_match = 0; bool match_available = false;
    size_t inserted_upto = 0;      // positions < inserted_upto are in the hash (over-insertion after a resync)
    static constexpr uint32_t NIL = 0;

    void init(const Params& p, const uint8_t* data, size_t n) {
        P = p; in = data; N = n;
        wsize = 1u << P.wbits; wmask = wsize - 1; hmask = (1u << P.hash_bits) - 1; hshift = (P.hash_bits + MIN_MATCH - 1) / MIN_MATCH;
        max_dist = wsize - (MAX_MATCH + MIN_MATCH + 1);
        buf.assign(data, data + n); buf.resize(n + MAX_MATCH + 8, 0);
        if (P.stale) {
            bool slid = false; size_t base = 0;
            while (N >= base + 2 * (size_t)wsize - (MAX_MATCH + MIN_MATCH + 1)) { base += wsize; slid = true; }
            if (slid) for (size_t p2 = N; p2 < N + MAX_MATCH; p2++) if (p2 >= wsize && p2 - wsize < N) buf[p2] = in[p2 - wsize];
        }
        head.assign(hmask + 1u, NIL); prev.assign(wsize, NIL);
        max_insert = N >= MIN_MATCH ? N - MIN_MATCH : 0;
        strstart = 0; inserted_upto = 0; match_length = prev_length = MIN_MATCH - 1; match_start = prev_match = 0; match_available = false;
        ins_h = 0;
        if (N >= 2) { ins_h = buf[0]; ins_h = ((ins_h << hshift) ^ buf[1]) & hmask; }
    }
    size_t lookahead() const { return N > strstart ? N - strstart : 0; }

    // insert position str into the hash (rolling hash), return previous head
    inline uint32_t insert(uint32_t str) {
        ins_h = ((ins_h << hshift) ^ buf[str + 2]) & hmask;
        uint32_t h = head[ins_h];
        prev[str & wmask] = h; head[ins_h] = str;
        if ((size_t)str + 1 > inserted_upto) inserted_upto = str + 1;
        return h;
    }
    inline void reset_hash_at(uint32_t pos) { ins_h = buf[pos]; ins_h = ((ins_h << hshift) ^ buf[pos + 1]) & hmask; }

    // zlib-style longest match search from candidate cur. Returns length; sets match_start on improvement.
    uint32_t longest_match(uint32_t cur) {
        unsigned chain_length = P.chain;
        const uint8_t* scan = &buf[strstart];
        uint32_t best_len = prev_length;
        uint32_t nice = P.nice;
        uint32_t limit = strstart > max_dist ? strstart - max_dist : NIL;
        if (prev_length >= (uint32_t)P.good) chain_length >>= 2;
        size_t la = lookahead();
        if (nice > la) nice = (uint32_t)la;
        const uint8_t* strend = scan + MAX_MATCH;
        do {
            if (cur >= strstart) continue;   // only after a resync over-insertion
            const uint8_t* m = &buf[cur];
            if (m[best_len] != scan[best_len] || m[best_len - 1] != scan[best_len - 1] || m[0] != scan[0] || m[1] != scan[1]) continue;
            const uint8_t* s2 = scan + 2; const uint8_t* m2 = m + 2;
            while (s2 < strend && *s2 == *m2) { s2++; m2++; }
            uint32_t len = (uint32_t)(s2 - scan);
            if (len > best_len) {
                match_start = cur; best_len = len;
                if (len >= nice) break;
            }
        } while ((cur = prev[cur & wmask]) > limit && --chain_length != 0);
        return best_len <= la ? best_len : (uint32_t)la;
    }

    // ---- prediction interface: next token zlib would emit at position strstart (or strstart-1 in lazy mode)
    struct Pred { Token tok; uint32_t pos; };

    // helper: compute match at current strstart under the slow-mode rules (returns match_length, sets match_start)
    inline uint32_t slow_find(uint32_t hash_head) {
        uint32_t ml = MIN_MATCH - 1;
        if (hash_head != NIL && prev_length < (uint32_t)P.lazy && strstart - hash_head <= max_dist) {
            ml = longest_match(hash_head);
            if (ml <= 5 && (P.filtered || (P.too_far && ml == MIN_MATCH && strstart - match_start > 4096))) ml = MIN_MATCH - 1;
        }
        return ml;
    }

    Pred predict() {
        Pred r;
        if (P.huff || P.stored) { r.pos = strstart; r.tok = {(uint16_t)buf[strstart], 0}; return r; }
        if (P.rle) {
            r.pos = strstart; size_t la = lookahead();
            if (la >= MIN_MATCH && strstart > 0) {
                const uint8_t* s = &buf[strstart]; uint8_t prevb = buf[strstart - 1];
                if (prevb == s[0] && prevb == s[1] && prevb == s[2]) {
                    uint32_t len = 3; uint32_t lim = (uint32_t)std::min<size_t>(la, MAX_MATCH);
                    while (len < lim && s[len] == prevb) len++;
                    r.tok = {(uint16_t)len, 1}; return r;
                }
            }
            r.tok = {(uint16_t)buf[strstart], 0}; return r;
        }
        if (P.fast) {
            uint32_t hash_head = NIL;
            if (lookahead() >= MIN_MATCH) hash_head = (strstart < inserted_upto) ? prev[strstart & wmask] : insert(strstart);
            if (hash_head != NIL && strstart - hash_head <= max_dist) match_length = longest_match(hash_head);
            r.pos = strstart;
            if (match_length >= MIN_MATCH) r.tok = {(uint16_t)match_length, (uint32_t)(strstart - match_start)};
            else r.tok = {(uint16_t)buf[strstart], 0};
            return r;
        }
        // slow (lazy) mode: iterate positions until a token is determined
        for (;;) {
            uint32_t hash_head = NIL;
            if (lookahead() >= MIN_MATCH) hash_head = (strstart < inserted_upto) ? prev[strstart & wmask] : insert(strstart);
            prev_length = match_length; prev_match = match_start;
            match_length = slow_find(hash_head);
            if (prev_length >= MIN_MATCH && match_length <= prev_length) {
                r.pos = strstart - 1; r.tok = {(uint16_t)prev_length, (uint32_t)(strstart - 1 - prev_match)};
                return r;   // commit() advances
            } else if (match_available) {
                r.pos = strstart - 1; r.tok = {(uint16_t)buf[strstart - 1], 0};
                return r;
            } else {
                match_available = true; strstart++;
                if (strstart >= N) { // end of input: pending literal
                    r.pos = strstart - 1; r.tok = {(uint16_t)buf[strstart - 1], 0};
                    return r;
                }
            }
        }
    }

    // advance the state along the actual token `a` at position pos (the position predict() reported)
    void commit(const Pred& pr, const Token& a) {
        uint32_t pos = pr.pos;
        if (P.huff || P.stored || P.rle) { strstart = pos + (a.dist ? a.len : 1); return; }
        if (P.fast) {
            if (a.dist) {
                uint32_t len = a.len;
                if (len <= (uint32_t)P.lazy && N - strstart - len >= MIN_MATCH) {   // insert all, like zlib's max_insert_length rule
                    for (uint32_t k = 1; k < len; k++) { strstart++; if (strstart >= inserted_upto) insert(strstart); }
                    strstart++;
                } else { strstart += len; match_length = 0; if (strstart + 1 < buf.size()) reset_hash_at(strstart); }
                match_length = MIN_MATCH - 1;
            } else { strstart++; }
            return;
        }
        // lazy mode
        bool pred_match = pr.tok.dist != 0;
        if (a.dist == 0) {
            if (!pred_match) {   // predicted literal, actual literal: state is already at pos+2 with match pending (or at end)
                if (strstart < N) { strstart++; }   // the iteration that emitted the literal ends with strstart++
                else { match_available = false; }
                return;
            }
            // predicted a match at pos, actual literal: resync as if zlib emitted the literal at pos and has
            // position pos+1 pending with a fresh match search there
            strstart = pos + 1; match_available = true;
            if (strstart >= N) { match_available = false; return; }
            uint32_t hash_head = NIL;
            if (lookahead() >= MIN_MATCH) hash_head = (strstart < inserted_upto) ? prev[strstart & wmask] : insert(strstart);
            prev_length = MIN_MATCH - 1;
            match_length = slow_find(hash_head);
            strstart++;
            return;
        }
        // actual is a match of length L at pos
        uint32_t L = a.len;
        if (pred_match && a.len == pr.tok.len && a.dist == pr.tok.dist) {
            // zlib's own path: insert pos+1 .. pos+L-1 (pos+1 already inserted), then strstart = pos+L
            uint32_t end = pos + L - 1;
            for (uint32_t k = strstart + 1; k <= end; k++) { if ((size_t)k <= max_insert && k >= inserted_upto) insert(k); }
            strstart = pos + L; match_available = false; match_length = MIN_MATCH - 1;
            return;
        }
        // resync after a different token
        for (uint32_t k = pos + 1; k <= pos + L - 1; k++) { if ((size_t)k <= max_insert && k >= inserted_upto) insert(k); }
        strstart = pos + L; match_available = false; match_length = MIN_MATCH - 1; prev_length = MIN_MATCH - 1;
    }

    bool done() const { return strstart >= N && !match_available; }
};

} // namespace hd
