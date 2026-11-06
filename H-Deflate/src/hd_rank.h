// hd_rank.h - H-Deflate "rank" predictor (record version 2) for deflate streams made by encoders outside the zlib
// family (optimal parsers, Office, Apple, ...). At each token the predictor lists the useful match candidates
// (nearest first, strictly longer as they get farther), predicts a default token by a simple rule, and the actual
// token is coded as "as predicted" / literal / candidate rank (+ shorter length) / explicit. The codes go into
// dense per-field streams so the entropy coder behind us (HX Complete) can model them. Our own design.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See H-Deflate/LICENSE.txt.
#pragma once
#include "hd_diff.h"
#include <cmath>

namespace hd {

struct RankParams {
    int wbits = 15;      // window
    int hash_bits = 17;  // 3-byte hash table size
    int chain = 2048;    // candidates examined per position
    int lazy = 1;        // 0 greedy, 1 lazy (literal if the next position has a longer match), 2 lazy needs +2
    int min_len = 3;     // shortest match the default rule emits
    int far3 = 32768;    // len-3 matches farther than this are not emitted by default
    int far4 = 32768;    // len-4 likewise
    int full_dist = 1;   // 1: distances up to the full window (32768), 0: zlib's 32506 limit
    int litcost = 0;     // >0: emit a match by default only if its coded cost beats len * litcost bits (price rule)
    std::string name() const { char b[96]; snprintf(b, sizeof b, "rank w%d h%d c%d lz%d ml%d f3=%d f4=%d fd%d lc%d", wbits, hash_bits, chain, lazy, min_len, far3, far4, full_dist, litcost); return b; }
};

enum { R_SAME = 0, R_LIT = 1, R_RANK0 = 2, R_RANKS = 8, R_SHORT0 = 10, R_EXPLICIT = 20, R_FAR = 21 };

struct Cand { uint16_t len; uint32_t dist; };

class RankMatcher {
public:
    RankParams P; const uint8_t* buf = nullptr; size_t N = 0;
    std::vector<uint8_t> pad;   // input + zero padding
    std::vector<uint32_t> head, prev; uint32_t wmask, hmask; uint32_t max_dist;
    size_t inserted = 0;        // positions < inserted are in the hash
    uint8_t plen[288], pdlen[32];   // price model: code lengths of the previous block (static trees at first)
    void set_prices(const uint8_t* l, const uint8_t* d) { memcpy(plen, l, 288); memcpy(pdlen, d, 32); }
    void init(const RankParams& p, const uint8_t* data, size_t n) {
        set_prices(stat().llen, stat().dlen);
        P = p; N = n; pad.assign(data, data + n); pad.resize(n + MAX_MATCH + 8, 0); buf = pad.data();
        uint32_t wsize = 1u << P.wbits; wmask = wsize - 1; hmask = (1u << P.hash_bits) - 1;
        max_dist = P.full_dist ? wsize : wsize - (MAX_MATCH + MIN_MATCH + 1);
        head.assign(hmask + 1u, 0); prev.assign(wsize, 0); inserted = 0;
    }
    inline uint32_t hash3(size_t p) const { uint32_t v = (uint32_t)buf[p] | ((uint32_t)buf[p + 1] << 8) | ((uint32_t)buf[p + 2] << 16); return (v * 2654435761u) >> (32 - P.hash_bits); }
    inline void insert_upto(size_t pos) {   // insert all positions < pos (that have 3 bytes)
        size_t lim = N >= MIN_MATCH ? N - MIN_MATCH + 1 : 0;
        for (size_t p = inserted; p < pos && p < lim; p++) { uint32_t h = hash3(p); prev[p & wmask] = head[h]; head[h] = (uint32_t)p + 1; }   // +1 so 0 = none
        if (pos > inserted) inserted = pos;
    }
    // number of positions nearer than `dist` whose match length at pos is >= len; -1 if the chain walk does not reach dist
    int count_nearer(size_t pos, int len, uint32_t dist) const {
        uint32_t cur = head[hash3(pos)]; int chain = P.chain; int j = 0; const uint8_t* s = buf + pos;
        while (cur && chain-- > 0) {
            size_t cp = cur - 1; if (cp >= pos) { cur = prev[cp & wmask]; continue; } size_t d = pos - cp; if (d > max_dist) break;
            if (d == dist) return j;
            if (d < dist) { const uint8_t* m = buf + cp; int l = 0; while (l < len && m[l] == s[l]) l++; if (l >= len) j++; }
            cur = prev[cp & wmask];
        }
        return -1;
    }
    // inverse: the distance of the j-th position (nearest first) with match length >= len; 0 if none
    uint32_t find_jth(size_t pos, int len, int j) const {
        uint32_t cur = head[hash3(pos)]; int chain = P.chain; const uint8_t* s = buf + pos;
        while (cur && chain-- > 0) {
            size_t cp = cur - 1; if (cp >= pos) { cur = prev[cp & wmask]; continue; } size_t d = pos - cp; if (d > max_dist) break;
            const uint8_t* m = buf + cp; int l = 0; while (l < len && m[l] == s[l]) l++;
            if (l >= len) { if (j == 0) return (uint32_t)d; j--; }
            cur = prev[cp & wmask];
        }
        return 0;
    }
    // candidate list at pos: nearest first, each strictly longer than the previous; at most K entries
    int candidates(size_t pos, Cand* out, int K) const {
        int n = 0; if (pos + MIN_MATCH > N) return 0;
        uint32_t cur = head[hash3(pos)]; int chain = P.chain; uint32_t best = MIN_MATCH - 1;
        size_t maxlen = std::min<size_t>(MAX_MATCH, N - pos);
        const uint8_t* s = buf + pos;
        while (cur && chain-- > 0) {
            size_t cp = cur - 1; if (cp >= pos) { cur = prev[cp & wmask]; continue; }
            size_t d = pos - cp; if (d > max_dist) break;
            const uint8_t* m = buf + cp;
            if (m[best] == s[best] && m[0] == s[0] && m[1] == s[1] && m[2] == s[2]) {
                size_t l = 3; while (l < maxlen && m[l] == s[l]) l++;
                if (l > best) { best = (uint32_t)l; if (n < K) out[n++] = {(uint16_t)l, (uint32_t)d}; else { out[K - 1] = {(uint16_t)l, (uint32_t)d}; } if (l >= maxlen) break; }
            }
            cur = prev[cp & wmask];
        }
        return n;
    }
};

// per-stream dense sections
struct RankStreams { std::vector<uint8_t> kind, lens, expl; };

inline int match_bits(int len, int dist) {   // rough static cost of a match in bits
    const Tables& T = tables(); int lc = T.lcode(len); int dc = T.dcode(dist);
    return 7 + extra_lbits[lc] + 5 + extra_dbits[dc];
}
inline bool rule_emit(const RankParams& P, const Cand& c, const RankMatcher* M = nullptr, size_t pos = 0) {
    if (c.len < P.min_len) return false;
    if (P.litcost == 9 && M) {   // adaptive price rule: previous block's code lengths
        const Tables& T = tables(); int lc = T.lcode(c.len); int dc = T.dcode((int)c.dist);
        int mb = (M->plen[257 + lc] ? M->plen[257 + lc] : 15) + extra_lbits[lc] + (M->pdlen[dc] ? M->pdlen[dc] : 15) + extra_dbits[dc];
        int lb = 0; for (int k = 0; k < c.len; k++) { uint8_t b = M->buf[pos + k]; lb += M->plen[b] ? M->plen[b] : 15; }
        return mb < lb;
    }
    if (P.litcost && P.litcost != 9 && match_bits(c.len, (int)c.dist) >= c.len * P.litcost) return false;
    if (c.len == 3 && (int)c.dist > P.far3) return false;
    if (c.len == 4 && (int)c.dist > P.far4) return false;
    return true;
}

// default token at pos given candidates there (longest = cands[n-1]) and the lazy rule
inline Token default_token(const RankParams& P, RankMatcher& M, size_t pos, const Cand* c, int n) {
    if (n == 0 || !rule_emit(P, c[n - 1], &M, pos)) return {M.buf[pos], 0};
    if (P.lazy && pos + 1 < M.N) {
        M.insert_upto(pos + 1);
        Cand c2[8]; int n2 = M.candidates(pos + 1, c2, 8);
        if (n2 && rule_emit(P, c2[n2 - 1], &M, pos + 1) && c2[n2 - 1].len >= c[n - 1].len + (P.lazy == 2 ? 2 : 1)) return {M.buf[pos], 0};
    }
    return {c[n - 1].len, c[n - 1].dist};
}

inline void put_rank_params(std::vector<uint8_t>& v, const RankParams& P) {
    v.push_back((uint8_t)P.wbits); v.push_back((uint8_t)P.hash_bits); put_var(v, P.chain); v.push_back((uint8_t)P.lazy); v.push_back((uint8_t)P.min_len); put_var(v, P.far3); put_var(v, P.far4); v.push_back((uint8_t)P.full_dist); v.push_back((uint8_t)P.litcost);
}
inline RankParams get_rank_params(VarReader& r) {
    RankParams P; P.wbits = r.byte(); P.hash_bits = r.byte(); P.chain = (int)r.get(); P.lazy = r.byte(); P.min_len = r.byte(); P.far3 = (int)r.get(); P.far4 = (int)r.get(); P.full_dist = r.byte(); P.litcost = r.byte();
    if (P.wbits < 8 || P.wbits > 15 || P.hash_bits < 8 || P.hash_bits > 22) throw std::runtime_error("bad rank params");
    return P;
}

// code the actual token against the prediction; returns false if it needs the explicit path
inline void code_token(const RankParams& P, const RankMatcher& M, size_t pos, const Cand* c, int n, const Token& pred, const Token& a, RankStreams& S) {
    (void)P;
    if (a.len == pred.len && a.dist == pred.dist) { S.kind.push_back(R_SAME); return; }
    if (a.dist == 0) { S.kind.push_back(R_LIT); return; }
    // rank from the longest: r = 0 longest, 1 next shorter-nearer, ...
    for (int i = n - 1, r = 0; i >= 0 && r < R_RANKS; i--, r++) {
        if (c[i].dist == a.dist) {
            if (a.len == c[i].len) { S.kind.push_back((uint8_t)(R_RANK0 + r)); return; }
            if (a.len < c[i].len) { S.kind.push_back((uint8_t)(R_SHORT0 + r)); put_var(S.lens, c[i].len - a.len); return; }
            break;
        }
    }
    int j = (a.dist <= M.max_dist && a.len <= 258) ? M.count_nearer(pos, a.len, a.dist) : -1;
    if (j >= 0 && (uint32_t)M.find_jth(pos, a.len, j) == a.dist) { S.kind.push_back(R_FAR); put_var(S.lens, a.len - MIN_MATCH); put_var(S.expl, j); return; }
    S.kind.push_back(R_EXPLICIT); put_var(S.expl, a.len - MIN_MATCH); put_var(S.expl, a.dist - 1);
}

struct Pack2Result { bool ok = false, complete = false; std::vector<uint8_t> rec; size_t corrections = 0, tokens_done = 0; double est_bits = 0; std::string err; };

// order-0 entropy estimate of a byte stream (bits)
inline double entropy_bits(const std::vector<uint8_t>& v) {
    if (v.empty()) return 0; size_t f[256] = {0}; for (uint8_t b : v) f[b]++;
    double bits = 0; for (int i = 0; i < 256; i++) if (f[i]) bits -= f[i] * std::log2((double)f[i] / v.size());
    return bits + 8 * 16;
}

inline Pack2Result pack_stream2(const Inflated& R, const uint8_t* comp, const RankParams& P, size_t token_limit = 0) {
    Pack2Result res; const size_t N = R.raw.size();
    RankMatcher M; M.init(P, R.raw.data(), N);
    std::vector<uint8_t> hdr; hdr.push_back(2); put_rank_params(hdr, P); put_var(hdr, R.blocks.size());
    std::vector<uint8_t> blk; RankStreams S;
    std::vector<Token> btok; Cand c[8]; size_t blocks_done = 0;
    for (size_t bi = 0; bi < R.blocks.size(); bi++) {
        const Block& B = R.blocks[bi];
        uint8_t flags = (uint8_t)(B.type | (B.final ? F_FINAL : 0));
        btok.clear(); size_t cur = B.raw_begin;
        auto step = [&](const Token* actual_or_null, size_t remaining) {
            M.insert_upto(cur);
            int n = M.candidates(cur, c, 8);
            Token pred = default_token(P, M, cur, c, n);
            Token a = actual_or_null ? *actual_or_null : pred;
            if (!actual_or_null && a.dist && a.len > remaining) { if (remaining >= MIN_MATCH) a.len = (uint16_t)remaining; else a = {R.raw[cur], 0}; }
            code_token(P, M, cur, c, n, pred, a, S);
            if (S.kind.back() != R_SAME) res.corrections++;
            btok.push_back(a); cur += a.dist ? a.len : 1;
        };
        if (B.type == 0) { flags |= F_COUNT; while (cur < B.raw_end) step(nullptr, B.raw_end - cur); }
        else {
            for (size_t i = B.tok_begin; i < B.tok_end; i++) step(&R.tok[i], 0);
            if (cur != B.raw_end) { res.err = "token/raw mismatch"; return res; }
            flags |= F_COUNT;   // v2: block symbol counts always explicit (other encoders split freely)
        }
        res.tokens_done += btok.size();
        bool hdr_explicit = false;
        if (B.type == 2) { BlockPlan plan = plan_block(btok.data(), btok.size(), B.raw_end - B.raw_begin, true, false); if (!(plan.type == 2 && plan.hdr == B.hdr)) hdr_explicit = true; M.set_prices(B.hdr.llen, B.hdr.dlen); }
        else if (B.type == 1) M.set_prices(stat().llen, stat().dlen);
        if (hdr_explicit) flags |= F_HDR;
        if (!B.alt258.empty()) flags |= F_ALT;
        blk.push_back(flags);
        put_var(blk, B.type == 0 ? (B.raw_end - B.raw_begin) : (B.tok_end - B.tok_begin));
        if (hdr_explicit) { put_var(blk, B.hdr_nbits); std::vector<uint8_t> bits = copy_bits(comp, B.hdr_bitpos, B.hdr_nbits); blk.insert(blk.end(), bits.begin(), bits.end()); }
        if (flags & F_ALT) { put_var(blk, B.alt258.size()); uint32_t last = 0; for (uint32_t x : B.alt258) { put_var(blk, x - last); last = x; } }
        blocks_done++;
        if (token_limit && res.tokens_done >= token_limit && bi + 1 < R.blocks.size()) break;
    }
    res.complete = blocks_done == R.blocks.size();
    // assemble: header | blk len, blk | kind len, kind | lens len, lens | expl len, expl
    std::vector<uint8_t>& o = res.rec; o = hdr;
    put_var(o, blk.size()); o.insert(o.end(), blk.begin(), blk.end());
    put_var(o, S.kind.size()); o.insert(o.end(), S.kind.begin(), S.kind.end());
    put_var(o, S.lens.size()); o.insert(o.end(), S.lens.begin(), S.lens.end());
    put_var(o, S.expl.size()); o.insert(o.end(), S.expl.begin(), S.expl.end());
    res.est_bits = entropy_bits(S.kind) + 8.0 * (hdr.size() + blk.size()) + 6.5 * (S.lens.size() + S.expl.size());
    res.ok = true;
    return res;
}

inline bool unpack_stream2(const uint8_t* raw, size_t N, const uint8_t* rec, size_t rec_len, std::vector<uint8_t>& out, std::string& err) {
    try {
        VarReader r(rec, rec_len);
        if (r.byte() != 2) { err = "record version"; return false; }
        RankParams P = get_rank_params(r); size_t nblocks = r.get();
        size_t nblk = r.get(); VarReader rb(rec + r.i, nblk); r.i += nblk;
        size_t nk = r.get(); const uint8_t* kind = rec + r.i; r.i += nk;
        size_t nl = r.get(); VarReader rl(rec + r.i, nl); r.i += nl;
        size_t ne = r.get(); VarReader re(rec + r.i, ne); r.i += ne;
        if (r.i > rec_len) { err = "record truncated"; return false; }
        RankMatcher M; M.init(P, raw, N);
        BitWriter w; std::vector<Token> btok; std::vector<uint32_t> alt; Cand c[8];
        size_t cur = 0, ki = 0;
        for (size_t bi = 0; bi < nblocks; bi++) {
            uint8_t flags = rb.byte(); int type = flags & F_TYPE; bool final = flags & F_FINAL;
            size_t count = rb.get();
            std::vector<uint8_t> hdrbits; size_t hdr_nbits = 0;
            if (flags & F_HDR) { hdr_nbits = rb.get(); hdrbits.resize((hdr_nbits + 7) / 8); for (auto& b : hdrbits) b = rb.byte(); }
            alt.clear();
            if (flags & F_ALT) { size_t na = rb.get(); uint32_t last = 0; for (size_t k = 0; k < na; k++) { last += (uint32_t)rb.get(); alt.push_back(last); } }
            btok.clear(); size_t block_begin = cur;
            size_t end_raw = type == 0 ? cur + count : N; size_t ntok = type == 0 ? SIZE_MAX : count;
            while (btok.size() < ntok && cur < end_raw) {
                if (ki >= nk) { err = "kind stream short"; return false; }
                M.insert_upto(cur);
                int n = M.candidates(cur, c, 8);
                Token a = default_token(P, M, cur, c, n);
                uint8_t k = kind[ki++];
                if (k == R_LIT) a = {raw[cur], 0};
                else if (k >= R_RANK0 && k < R_RANK0 + R_RANKS) { int i = n - 1 - (k - R_RANK0); if (i < 0) { err = "bad rank"; return false; } a = {c[i].len, c[i].dist}; }
                else if (k >= R_SHORT0 && k < R_SHORT0 + R_RANKS) { int i = n - 1 - (k - R_SHORT0); if (i < 0) { err = "bad rank"; return false; } a = {(uint16_t)(c[i].len - rl.get()), c[i].dist}; }
                else if (k == R_EXPLICIT) { a.len = (uint16_t)(re.get() + MIN_MATCH); a.dist = (uint32_t)(re.get() + 1); }
                else if (k == R_FAR) { a.len = (uint16_t)(rl.get() + MIN_MATCH); a.dist = M.find_jth(cur, a.len, (int)re.get()); if (!a.dist) { err = "far candidate missing"; return false; } }
                else if (k != R_SAME) { err = "bad kind"; return false; }
                size_t adv = a.dist ? a.len : 1;
                if (cur + adv > end_raw || (a.dist && a.dist > cur) || (a.dist && a.len < MIN_MATCH)) { err = "bad token"; return false; }
                btok.push_back(a); cur += adv;
            }
            w.put(final ? 1 : 0, 1); w.put((uint32_t)type, 2);
            if (type == 0) { w.align(); uint32_t len = (uint32_t)(cur - block_begin); if (len > 65535) { err = "stored too long"; return false; } w.put(len, 16); w.put(len ^ 0xffff, 16); w.out.insert(w.out.end(), raw + block_begin, raw + cur); }
            else {
                const uint8_t* llen; const uint8_t* dlen; DynHeader H;
                if (type == 1) { llen = stat().llen; dlen = stat().dlen; }
                else if (flags & F_HDR) { BitReader br(hdrbits.data(), hdrbits.size()); if (!parse_dyn_header(br, H)) { err = "bad explicit header"; return false; } for (size_t i = 0; i < hdr_nbits; i++) w.put((hdrbits[i >> 3] >> (i & 7)) & 1, 1); llen = H.llen; dlen = H.dlen; }
                else { BlockPlan plan = plan_block(btok.data(), btok.size(), cur - block_begin, true, false); if (plan.type != 2) { err = "predicted header not dynamic"; return false; } H = plan.hdr; write_dyn_header(w, H); llen = H.llen; dlen = H.dlen; }
                write_tokens(w, btok.data(), btok.size(), llen, dlen, alt.empty() ? nullptr : &alt);
                M.set_prices(llen, dlen);
            }
            if (final) break;
        }
        w.finish(); out = std::move(w.out); return true;
    } catch (std::exception& e) { err = e.what(); return false; }
}

} // namespace hd

namespace hd {
inline bool unpack_any(const uint8_t* raw, size_t N, const uint8_t* rec, size_t rec_len, std::vector<uint8_t>& out, std::string& err) {
    if (!rec_len) { err = "empty record"; return false; }
    return rec[0] == 2 ? unpack_stream2(raw, N, rec, rec_len, out, err) : unpack_stream(raw, N, rec, rec_len, out, err);
}
}
