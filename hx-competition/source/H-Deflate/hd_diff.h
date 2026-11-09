// hd_diff.h - H-Deflate reconstruction record: predict the token stream and block structure with the matcher,
// store only the differences; rebuild the exact deflate bytes from raw data + record.
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#pragma once
#include "hd_match.h"
#include "hd_inflate.h"

namespace hd {

enum { F_TYPE = 3, F_FINAL = 4, F_COUNT = 8, F_HDR = 16, F_ALT = 32 };
enum { C_LIT = 1, C_MATCH = 2, C_LEN = 3, C_DIST = 4, C_BOTH = 5 };

struct Correction { uint32_t idx; uint8_t kind; uint16_t len; uint32_t dist; };

inline void put_params(std::vector<uint8_t>& v, const Params& P) {
    v.push_back((uint8_t)P.wbits); v.push_back((uint8_t)P.hash_bits); v.push_back((uint8_t)P.mem_log);
    put_var(v, P.good); put_var(v, P.lazy); put_var(v, P.nice); put_var(v, P.chain);
    uint8_t fl = (P.fast ? 1 : 0) | (P.filtered ? 2 : 0) | (P.fixed ? 4 : 0) | (P.rle ? 8 : 0) | (P.huff ? 16 : 0) | (P.stored ? 32 : 0) | (P.too_far ? 64 : 0) | (P.stale ? 128 : 0);
    v.push_back(fl); v.push_back((uint8_t)P.level_hint);
}
inline Params get_params(VarReader& r) {
    Params P; P.wbits = r.byte(); P.hash_bits = r.byte(); P.mem_log = r.byte();
    P.good = (int)r.get(); P.lazy = (int)r.get(); P.nice = (int)r.get(); P.chain = (int)r.get();
    uint8_t fl = r.byte(); P.fast = fl & 1; P.filtered = fl & 2; P.fixed = fl & 4; P.rle = fl & 8; P.huff = fl & 16; P.stored = fl & 32; P.too_far = fl & 64; P.stale = fl & 128;
    P.level_hint = r.byte();
    if (P.wbits < 8 || P.wbits > 15 || P.hash_bits < 8 || P.hash_bits > 16 || P.mem_log < 1 || P.mem_log > 9) throw std::runtime_error("bad params");
    return P;
}

// base of the zlib window at the moment a block ending with an iteration at `last_iter` is flushed
inline size_t window_base_at(const Params& P, size_t last_iter) {
    size_t wsize = (size_t)1 << P.wbits; size_t thr = 2 * wsize - (MAX_MATCH + MIN_MATCH + 1) + 1;   // strstart >= 2*wsize - 261
    if (last_iter < thr) return 0;
    return wsize * ((last_iter - thr) / wsize + 1);
}

// copy nbits starting at bit position `bp` of `src` into a byte vector (LSB-first packing)
inline std::vector<uint8_t> copy_bits(const uint8_t* src, size_t bp, size_t nbits) {
    std::vector<uint8_t> out((nbits + 7) / 8, 0);
    for (size_t i = 0; i < nbits; i++) { size_t b = bp + i; if ((src[b >> 3] >> (b & 7)) & 1) out[i >> 3] |= (uint8_t)(1 << (i & 7)); }
    return out;
}

struct PackResult { bool ok = false; bool complete = false; std::vector<uint8_t> rec; size_t corrections = 0; size_t tokens_done = 0; std::string err; };

// Build the record for an inflated stream R using params P. If token_limit > 0, stop after that many tokens (scoring).
inline PackResult pack_stream(const Inflated& R, const uint8_t* comp, const Params& P, size_t token_limit = 0) {
    PackResult res;
    const size_t N = R.raw.size();
    Matcher M; M.init(P, R.raw.data(), N);
    std::vector<uint8_t>& rec = res.rec;
    rec.push_back(1);   // record version
    put_params(rec, P);
    put_var(rec, R.blocks.size());
    const size_t limit = ((size_t)1 << (P.mem_log + 6)) - 1;
    std::vector<Token> btok;
    for (size_t bi = 0; bi < R.blocks.size(); bi++) {
        const Block& B = R.blocks[bi];
        uint8_t flags = (uint8_t)(B.type | (B.final ? F_FINAL : 0));
        std::vector<Correction> corr;
        btok.clear();
        size_t cur = B.raw_begin;
        size_t last_iter = 0;
        if (B.type == 0) {
            flags |= F_COUNT;
            size_t stored_len = B.raw_end - B.raw_begin;
            while (cur < B.raw_end) {
                Matcher::Pred pr = M.predict();
                if (pr.pos != cur) { res.err = "predictor position drift"; return res; }
                Token a = pr.tok; size_t remaining = B.raw_end - cur;
                if (a.dist && a.len > remaining) {
                    if (remaining >= MIN_MATCH) { a.len = (uint16_t)remaining; corr.push_back({(uint32_t)btok.size(), C_LEN, a.len, a.dist}); }
                    else { a = {(uint16_t)R.raw[cur], 0}; corr.push_back({(uint32_t)btok.size(), C_LIT, 0, 0}); }
                }
                M.commit(pr, a); btok.push_back(a); cur += a.dist ? a.len : 1;
                last_iter = pr.pos + (P.fast || P.rle || P.huff || P.stored ? 0 : 1);
            }
            (void)stored_len;
        } else {
            size_t count = B.tok_end - B.tok_begin;
            if (!B.final && count != limit) flags |= F_COUNT;
            for (size_t i = B.tok_begin; i < B.tok_end; i++) {
                Matcher::Pred pr = M.predict();
                if (pr.pos != cur) { res.err = "predictor position drift"; return res; }
                const Token& a = R.tok[i];
                if (a.len != pr.tok.len || a.dist != pr.tok.dist) {
                    Correction c; c.idx = (uint32_t)(i - B.tok_begin); c.len = a.len; c.dist = a.dist;
                    if (a.dist == 0) c.kind = C_LIT;
                    else if (pr.tok.dist == 0) c.kind = C_MATCH;
                    else if (a.dist == pr.tok.dist) c.kind = C_LEN;
                    else if (a.len == pr.tok.len) c.kind = C_DIST;
                    else c.kind = C_BOTH;
                    corr.push_back(c);
                }
                M.commit(pr, a); btok.push_back(a); cur += a.dist ? a.len : 1;
                last_iter = pr.pos + (P.fast || P.rle || P.huff || P.stored ? 0 : 1);
            }
            if (cur != B.raw_end) { res.err = "token/raw mismatch"; return res; }
        }
        res.corrections += corr.size();
        res.tokens_done += btok.size();
        // block plan (header prediction)
        bool hdr_explicit = false;
        if (B.type == 2) {
            size_t base = window_base_at(P, last_iter);
            bool can_store = B.raw_begin >= base;
            BlockPlan plan = plan_block(btok.data(), btok.size(), B.raw_end - B.raw_begin, can_store, P.fixed);
            if (!(plan.type == 2 && plan.hdr == B.hdr)) hdr_explicit = true;
        }
        if (hdr_explicit) flags |= F_HDR;
        if (!B.alt258.empty()) flags |= F_ALT;
        rec.push_back(flags);
        if (flags & F_COUNT) put_var(rec, B.type == 0 ? (B.raw_end - B.raw_begin) : (B.tok_end - B.tok_begin));
        if (hdr_explicit) {
            put_var(rec, B.hdr_nbits);
            std::vector<uint8_t> bits = copy_bits(comp, B.hdr_bitpos, B.hdr_nbits);
            rec.insert(rec.end(), bits.begin(), bits.end());
        }
        if (flags & F_ALT) { put_var(rec, B.alt258.size()); uint32_t last = 0; for (uint32_t x : B.alt258) { put_var(rec, x - last); last = x; } }
        put_var(rec, corr.size());
        uint32_t last = 0;
        for (const Correction& c : corr) {
            put_var(rec, c.idx - last); last = c.idx;
            rec.push_back(c.kind);
            if (c.kind == C_MATCH || c.kind == C_BOTH) { put_var(rec, c.len - MIN_MATCH); put_var(rec, c.dist - 1); }
            else if (c.kind == C_LEN) put_var(rec, c.len - MIN_MATCH);
            else if (c.kind == C_DIST) put_var(rec, c.dist - 1);
        }
        if (token_limit && res.tokens_done >= token_limit && bi + 1 < R.blocks.size()) { res.ok = true; return res; }
    }
    res.ok = true; res.complete = true;
    return res;
}

// Rebuild the deflate bytes from raw + record.
inline bool unpack_stream(const uint8_t* raw, size_t N, const uint8_t* rec, size_t rec_len, std::vector<uint8_t>& out, std::string& err) {
    try {
        VarReader r(rec, rec_len);
        if (r.byte() != 1) { err = "record version"; return false; }
        Params P = get_params(r);
        size_t nblocks = r.get();
        Matcher M; M.init(P, raw, N);
        BitWriter w;
        const size_t limit = ((size_t)1 << (P.mem_log + 6)) - 1;
        std::vector<Token> btok; std::vector<uint32_t> alt;
        size_t cur = 0;
        for (size_t bi = 0; bi < nblocks; bi++) {
            uint8_t flags = r.byte(); int type = flags & F_TYPE; bool final = flags & F_FINAL;
            if (type == 3) { err = "bad type"; return false; }
            size_t count = 0; bool have_count = flags & F_COUNT;
            if (have_count) count = r.get();
            std::vector<uint8_t> hdrbits; size_t hdr_nbits = 0;
            if (flags & F_HDR) { hdr_nbits = r.get(); hdrbits.resize((hdr_nbits + 7) / 8); for (auto& b : hdrbits) b = r.byte(); }
            alt.clear();
            if (flags & F_ALT) { size_t na = r.get(); uint32_t last = 0; for (size_t k = 0; k < na; k++) { last += (uint32_t)r.get(); alt.push_back(last); } }
            size_t ncorr = r.get();
            std::vector<Correction> corr(ncorr); uint32_t last = 0;
            for (size_t k = 0; k < ncorr; k++) {
                Correction& c = corr[k]; last += (uint32_t)r.get(); c.idx = last; c.kind = r.byte(); c.len = 0; c.dist = 0;
                if (c.kind == C_MATCH || c.kind == C_BOTH) { c.len = (uint16_t)(r.get() + MIN_MATCH); c.dist = (uint32_t)(r.get() + 1); }
                else if (c.kind == C_LEN) c.len = (uint16_t)(r.get() + MIN_MATCH);
                else if (c.kind == C_DIST) c.dist = (uint32_t)(r.get() + 1);
                else if (c.kind != C_LIT) { err = "bad correction"; return false; }
            }
            // generate tokens
            btok.clear(); size_t block_begin = cur; size_t ci = 0; size_t last_iter = 0;
            size_t end_raw = (type == 0) ? cur + count : N;
            size_t ntok = (type == 0) ? SIZE_MAX : (final ? SIZE_MAX : (have_count ? count : limit));
            while (btok.size() < ntok && cur < end_raw) {
                Matcher::Pred pr = M.predict();
                if (pr.pos != cur) { err = "drift"; return false; }
                Token a = pr.tok;
                if (ci < corr.size() && corr[ci].idx == btok.size()) {
                    const Correction& c = corr[ci++];
                    switch (c.kind) {
                        case C_LIT: a = {(uint16_t)raw[cur], 0}; break;
                        case C_MATCH: case C_BOTH: a = {c.len, c.dist}; break;
                        case C_LEN: a.len = c.len; break;
                        case C_DIST: a.dist = c.dist; break;
                    }
                }
                size_t adv = a.dist ? a.len : 1;
                if (cur + adv > end_raw || (a.dist && a.dist > cur)) { err = "bad token"; return false; }
                M.commit(pr, a); btok.push_back(a); cur += adv;
                last_iter = pr.pos + (P.fast || P.rle || P.huff || P.stored ? 0 : 1);
            }
            if (ci != corr.size()) { err = "unused corrections"; return false; }
            // emit block
            w.put(final ? 1 : 0, 1); w.put((uint32_t)type, 2);
            if (type == 0) {
                w.align(); uint32_t len = (uint32_t)(cur - block_begin); if (len > 65535) { err = "stored too long"; return false; }
                w.put(len, 16); w.put(len ^ 0xffff, 16);
                w.out.insert(w.out.end(), raw + block_begin, raw + cur);
            } else {
                const uint8_t* llen; const uint8_t* dlen; DynHeader H;
                if (type == 1) { llen = stat().llen; dlen = stat().dlen; }
                else if (flags & F_HDR) {
                    BitReader br(hdrbits.data(), hdrbits.size());
                    if (!parse_dyn_header(br, H)) { err = "bad explicit header"; return false; }
                    for (size_t i = 0; i < hdr_nbits; i++) w.put((hdrbits[i >> 3] >> (i & 7)) & 1, 1);
                    llen = H.llen; dlen = H.dlen;
                } else {
                    size_t base = window_base_at(P, last_iter);
                    BlockPlan plan = plan_block(btok.data(), btok.size(), cur - block_begin, block_begin >= base, P.fixed);
                    if (plan.type != 2) { err = "predicted header not dynamic"; return false; }
                    H = plan.hdr; write_dyn_header(w, H); llen = H.llen; dlen = H.dlen;
                }
                write_tokens(w, btok.data(), btok.size(), llen, dlen, alt.empty() ? nullptr : &alt);
            }
            if (final) { if (bi + 1 != nblocks) { err = "final block not last"; return false; } break; }
        }
        w.finish();
        out = std::move(w.out);
        return true;
    } catch (std::exception& e) { err = e.what(); return false; }
}

} // namespace hd
