// hx_lzma.h - LZMA route for HX Complete 14 (Igor Pavlov's public-domain LZMA SDK C code), with the lzma-hi idea:
// per-file choice of literal context / position bits and delta filtering, screened on samples first (sample screening).
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See HX/LICENSE.txt.
// Body: [lc][lp][pb][fb][dictlog][delta 2 LE] + 5 LZMA props + LZMA data (no end mark; size known to the caller).
#pragma once
extern "C" {
#include "LzmaEnc.h"
#include "LzmaDec.h"
}
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hxlzma {
static void *la_alloc(ISzAllocPtr, size_t s) { return malloc(s); }
static void la_free(ISzAllocPtr, void *a) { free(a); }
static const ISzAlloc g_la = {la_alloc, la_free};

struct Cfg { int lc = 3, lp = 0, pb = 2, fb = 273, delta = 0; };

inline void delta_enc(uint8_t *p, size_t n, int d) { if (d <= 0) return; for (size_t i = n; i-- > (size_t)d;) p[i] -= p[i - d]; }
inline void delta_dec(uint8_t *p, size_t n, int d) { if (d <= 0) return; for (size_t i = d; i < n; i++) p[i] += p[i - d]; }

inline std::vector<uint8_t> enc(const uint8_t *in, size_t n, const Cfg &c, int threads = 2) {
    std::vector<uint8_t> src(in, in + n); delta_enc(src.data(), n, c.delta);
    unsigned dl = 16; while (dl < 30 && ((size_t)1 << dl) < n) dl++;
    CLzmaEncProps p; LzmaEncProps_Init(&p); p.level = 9; p.dictSize = 1u << dl; p.lc = c.lc; p.lp = c.lp; p.pb = c.pb; p.fb = c.fb; p.algo = 1; p.btMode = 1; p.numHashBytes = 4; p.mc = 1u << 30; p.numThreads = threads > 1 ? 2 : 1; p.writeEndMark = 0;
    std::vector<uint8_t> out(n + n / 3 + 1024);
    uint8_t hdr[7] = {(uint8_t)c.lc, (uint8_t)c.lp, (uint8_t)c.pb, (uint8_t)(c.fb > 255 ? 255 : c.fb), (uint8_t)dl, (uint8_t)(c.delta & 255), (uint8_t)(c.delta >> 8)};
    memcpy(out.data(), hdr, 7);
    SizeT dlen = out.size() - 12, plen = 5;
    SRes r = LzmaEncode(out.data() + 12, &dlen, src.data(), n, &p, out.data() + 7, &plen, 0, nullptr, &g_la, &g_la);
    if (r != SZ_OK || plen != 5) return {};
    out.resize(12 + dlen); return out;
}

inline bool dec(const uint8_t *body, size_t m, uint8_t *out, size_t n) {
    if (m < 12) return false;
    int delta = body[5] | (body[6] << 8);
    SizeT dlen = n, slen = m - 12; ELzmaStatus st;
    SRes r = LzmaDecode(out, &dlen, body + 12, &slen, body + 7, 5, LZMA_FINISH_END, &st, &g_la);
    if (r != SZ_OK || dlen != n) return false;
    delta_dec(out, n, delta); return true;
}

// candidates: default (3,0,2), binary-ish (0,2,2), (0,0,0), (1,0,0), (4,0,0), (0,3,3) - and the same with a delta at
// the detected record or sample width when one is given (the record-length idea from the -hi tools)
inline std::vector<Cfg> candidates(int rec) {
    std::vector<Cfg> v; int lcpb[][3] = {{3, 0, 2}, {0, 2, 2}, {0, 0, 0}, {1, 0, 0}, {4, 0, 0}, {0, 3, 3}, {2, 0, 0}, {0, 1, 1}};
    for (auto &t : lcpb) { Cfg c; c.lc = t[0]; c.lp = t[1]; c.pb = t[2]; v.push_back(c); }
    if (rec > 1 && rec < 65536) for (auto &t : lcpb) { Cfg c; c.lc = t[0]; c.lp = t[1]; c.pb = t[2]; c.delta = rec; v.push_back(c); }
    return v;
}
} // namespace hxlzma
