// entbench.cpp - entropy-coder bench for the biased-random test, 3 Nov 2026.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. All rights reserved - NOT MIT. See HX/LICENSE.txt.
// The entropy coders themselves are the standard public ones, unchanged: Huff0 and FSE (Yann Collet, BSD licence,
// github.com/Cyan4973/FiniteStateEntropy) and ryg_rans (Fabian Giesen, public domain, github.com/rygorous/ryg_rans;
// its frequency normaliser is copied below). "ac0" is an order-0 adaptive binary arithmetic coder in the style of
// Matt Mahoney's fpaq0 (written here). "hx" is OURS: the Harrington Exchange front end (per 1 MB segment it tries the
// data as it is, split into 2/3/4 byte lanes (each lane by Huff0 or rANS), or as 12-bit samples by rANS; keeps the smallest).
// Everything is done in memory; decode time = best of 5 (of 2 for decodes over 2 s); every result is checked byte for byte.
// "hxd" = OURS with the cascade's repeat line in front of hxs (exact repeats of earlier chunks become references).
// "hxs" = OURS with Harrington Sets in front: 64 KB windows sorted into up to 4 classes, each class coded by hx.
// usage: entbench MODE FILE [BLOCK_KB] [THREADS]      MODE = huf | fse | rans | ac0 | hx | hxs
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include "huf.h"
#include "fse.h"
#include "rans_byte.h"
using namespace std;
typedef vector<uint8_t> B;
static double now() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }
static void put32(B &o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 255); }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16(B &o, uint32_t v) { o.push_back(v & 255); o.push_back((v >> 8) & 255); }
static uint32_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }

// ---------------- static order-0 rANS over an alphabet of up to 4096 symbols (ryg_rans byte coder, 2 states)
static void normalize_freqs(vector<uint32_t> &freqs, vector<uint32_t> &cum, uint32_t target) {   // from ryg_rans main.cpp
    size_t A = freqs.size(); cum.assign(A + 1, 0);
    for (size_t i = 0; i < A; i++) cum[i + 1] = cum[i] + freqs[i];
    uint64_t cur = cum[A];
    for (size_t i = 1; i <= A; i++) cum[i] = (uint32_t)((uint64_t)target * cum[i] / cur);
    for (size_t i = 0; i < A; i++) if (freqs[i] && cum[i + 1] == cum[i]) {
        uint32_t bf = ~0u; long bs = -1;
        for (size_t j = 0; j < A; j++) { uint32_t f = cum[j + 1] - cum[j]; if (f > 1 && f < bf) { bf = f; bs = (long)j; } }
        assert(bs != -1);
        if ((size_t)bs < i) for (size_t j = bs + 1; j <= i; j++) cum[j]--; else for (size_t j = i + 1; j <= (size_t)bs; j++) cum[j]++;
    }
    for (size_t i = 0; i < A; i++) freqs[i] = cum[i + 1] - cum[i];
}
static void rans_enc(const uint16_t *s, size_t n, uint32_t A, B &o) {
    const uint32_t PB = 16;   // 16-bit probabilities (the most rans_byte allows) - fine-grained enough for very biased data
    vector<uint32_t> f(A, 0), cum; for (size_t i = 0; i < n; i++) f[s[i]]++;
    if (n == 0) { put16(o, 0); return; }
    normalize_freqs(f, cum, 1u << PB);
    uint32_t used = 0; for (uint32_t i = 0; i < A; i++) used += f[i] > 0;
    put16(o, used);
    if (A == 256) { uint8_t bm[32] = {0}; for (uint32_t i = 0; i < 256; i++) if (f[i]) bm[i >> 3] |= 1 << (i & 7);   // bitmap + frequencies
        o.insert(o.end(), bm, bm + 32); for (uint32_t i = 0; i < 256; i++) if (f[i]) put16(o, f[i] - 1); }
    else for (uint32_t i = 0; i < A; i++) if (f[i]) { put16(o, i); put16(o, f[i]); }
    vector<RansEncSymbol> es(A); for (uint32_t i = 0; i < A; i++) if (f[i]) RansEncSymbolInit(&es[i], cum[i], f[i], PB);
    B buf(n * 2 + 64); uint8_t *ptr = buf.data() + buf.size();
    RansState r0, r1; RansEncInit(&r0); RansEncInit(&r1);
    if (n & 1) RansEncPutSymbol(&r0, &ptr, &es[s[n - 1]]);
    for (size_t i = n & ~(size_t)1; i > 0; i -= 2) { RansEncPutSymbol(&r1, &ptr, &es[s[i - 1]]); RansEncPutSymbol(&r0, &ptr, &es[s[i - 2]]); }
    RansEncFlush(&r1, &ptr); RansEncFlush(&r0, &ptr);
    size_t len = buf.data() + buf.size() - ptr; put32(o, (uint32_t)len); o.insert(o.end(), ptr, ptr + len);
}
static size_t rans_dec(const uint8_t *p, uint16_t *s, size_t n, uint32_t A) {   // returns bytes read
    const uint32_t PB = 16;
    const uint8_t *p0 = p;
    uint32_t used = get16(p); p += 2; if (n == 0) return 2;
    vector<uint32_t> f(A, 0), cum(A + 1, 0);
    if (A == 256) { const uint8_t *bm = p; p += 32; for (uint32_t i = 0; i < 256; i++) if (bm[i >> 3] >> (i & 7) & 1) { f[i] = get16(p) + 1; p += 2; } }
    else for (uint32_t k = 0; k < used; k++) { uint32_t sym = get16(p), fr = get16(p + 2); p += 4; f[sym] = fr; }
    for (uint32_t i = 0; i < A; i++) cum[i + 1] = cum[i] + f[i];
    vector<RansDecSymbol> ds(A); vector<uint16_t> c2s(1u << PB);
    for (uint32_t i = 0; i < A; i++) if (f[i]) { RansDecSymbolInit(&ds[i], cum[i], f[i]); for (uint32_t k = cum[i]; k < cum[i + 1]; k++) c2s[k] = (uint16_t)i; }
    uint32_t len = get32(p); p += 4; uint8_t *ptr = (uint8_t *)p;
    RansState r0, r1; RansDecInit(&r0, &ptr); RansDecInit(&r1, &ptr);
    size_t i = 0;
    for (; i + 1 < n; i += 2) {
        uint32_t a = c2s[RansDecGet(&r0, PB)], b = c2s[RansDecGet(&r1, PB)]; s[i] = a; s[i + 1] = b;
        RansDecAdvanceSymbolStep(&r0, &ds[a], PB); RansDecAdvanceSymbolStep(&r1, &ds[b], PB); RansDecRenorm(&r0, &ptr); RansDecRenorm(&r1, &ptr);
    }
    if (n & 1) { uint32_t a = c2s[RansDecGet(&r0, PB)]; s[n - 1] = a; RansDecAdvanceSymbol(&r0, &ptr, &ds[a], PB); }
    return (p - p0) + len;
}

// ---------------- one block of bytes with Huff0 / FSE / rANS: [u8 type][u32 len][payload]
enum { RAW = 0, RLE = 1, HUF = 2, FSEC = 3, RANS = 4 };
static void enc_bytes(const string &m, const uint8_t *in, size_t n, B &o) {
    B tmp(HUF_compressBound(n) + n + 1024); size_t r = 0; int type = RAW;
    if (m == "huf") { r = HUF_compress(tmp.data(), tmp.size(), in, n); if (HUF_isError(r)) { fprintf(stderr, "huf error\n"); exit(1); } type = HUF; }
    else if (m == "fse") { r = FSE_compress(tmp.data(), tmp.size(), in, n); if (FSE_isError(r)) { fprintf(stderr, "fse error\n"); exit(1); } type = FSEC; }
    else if (m == "fse12") { r = FSE_compress2(tmp.data(), tmp.size(), in, n, 255, 12); if (FSE_isError(r)) { fprintf(stderr, "fse error\n"); exit(1); } type = FSEC; }
    else if (m == "rans") { vector<uint16_t> s(in, in + n); B t; rans_enc(s.data(), n, 256, t); r = t.size(); memcpy(tmp.data(), t.data(), r); type = RANS; }
    if (r == 1 && type != RANS) { o.push_back(RLE); put32(o, 1); o.push_back(in[0]); return; }
    if (r == 0 || r >= n) { o.push_back(RAW); put32(o, (uint32_t)n); o.insert(o.end(), in, in + n); return; }
    o.push_back(type); put32(o, (uint32_t)r); o.insert(o.end(), tmp.data(), tmp.data() + r);
}
static size_t dec_bytes(const uint8_t *p, uint8_t *out, size_t n) {
    int type = p[0]; uint32_t len = get32(p + 1); p += 5;
    if (type == RAW) memcpy(out, p, n);
    else if (type == RLE) memset(out, p[0], n);
    else if (type == HUF) { size_t r = HUF_decompress(out, n, p, len); if (HUF_isError(r)) { fprintf(stderr, "huf d error\n"); exit(1); } }
    else if (type == FSEC) { size_t r = FSE_decompress(out, n, p, len); if (FSE_isError(r) || r != n) { fprintf(stderr, "fse d error\n"); exit(1); } }
    else if (type == RANS) { vector<uint16_t> s(n); rans_dec(p, s.data(), n, 256); for (size_t i = 0; i < n; i++) out[i] = (uint8_t)s[i]; }
    return 5 + len;
}

// ---------------- ac0: order-0 adaptive binary arithmetic coder (fpaq0 style, adaptive rate 1/n up to 1/1024)
struct AC0 {
    uint32_t p[256]; uint16_t c[256]; uint32_t x1 = 0, x2 = 0xffffffff, x = 0;
    AC0() { for (int i = 0; i < 256; i++) { p[i] = 1u << 31; c[i] = 0; } }
    inline void upd(int ctx, int bit) { int n = c[ctx]; if (n < 1023) c[ctx]++; uint32_t q = p[ctx];
        if (bit) q += (uint32_t)(((uint64_t)(0xffffffffu - q)) / (n + 1.5)); else q -= (uint32_t)(q / (n + 1.5)); p[ctx] = q; }
    inline uint32_t mid(int ctx) { uint32_t q = p[ctx] >> 16; if (q < 1) q = 1; if (q > 65535) q = 65535; return x1 + (uint32_t)(((uint64_t)(x2 - x1) * q) >> 16); }
};
static void ac0_enc(const uint8_t *in, size_t n, B &o) {
    AC0 a; for (size_t i = 0; i < n; i++) { int ctx = 1;
        for (int k = 7; k >= 0; k--) { int bit = (in[i] >> k) & 1; uint32_t xm = a.mid(ctx);
            if (bit) a.x2 = xm; else a.x1 = xm + 1; a.upd(ctx, bit); ctx = ctx * 2 + bit;
            while (((a.x1 ^ a.x2) & 0xff000000) == 0) { o.push_back(a.x2 >> 24); a.x1 <<= 8; a.x2 = (a.x2 << 8) | 255; } } }
    o.push_back(a.x1 >> 24); o.push_back(255); o.push_back(255); o.push_back(255);
}
static void ac0_dec(const uint8_t *p, size_t m, uint8_t *out, size_t n) {
    AC0 a; size_t ip = 0; for (int k = 0; k < 4; k++) a.x = (a.x << 8) | (ip < m ? p[ip++] : 255);
    for (size_t i = 0; i < n; i++) { int ctx = 1;
        for (int k = 0; k < 8; k++) { uint32_t xm = a.mid(ctx); int bit = a.x <= xm;
            if (bit) a.x2 = xm; else a.x1 = xm + 1; a.upd(ctx, bit); ctx = ctx * 2 + bit;
            while (((a.x1 ^ a.x2) & 0xff000000) == 0) { a.x1 <<= 8; a.x2 = (a.x2 << 8) | 255; a.x = (a.x << 8) | (ip < m ? p[ip++] : 255); } }
        out[i] = (uint8_t)(ctx & 255); }
}

// ---------------- hx (ours): Harrington Exchange front end per segment, then Huff0 (lanes) or rANS (12-bit samples)
// transform ids: 0 = as is, 2/3/4 = split into that many byte lanes, 12 = unpack 12-bit samples (2 per 3 bytes)
// image lines: row width (in samples) found once per file - the lag that best predicts each sample from the one
// "above" it; 0 = no row structure better than the sample before
static size_t g_w16 = 0, g_w8 = 0;
static size_t detect_width(const uint8_t *in, size_t n, int bps) {
    size_t ns = n / bps; if (ns < 4096) return 0;
    auto S = [&](size_t i) -> int { return bps == 2 ? (in[2 * i] | (in[2 * i + 1] << 8)) : in[i]; };
    size_t step = max<size_t>(1, ns / 8192), best = 0; double bc = 1e300, sum = 0; size_t cnt = 0;
    for (size_t W = 8; W <= 16384 && W < ns / 4; W++) { double c = 0; for (size_t i = 16384 + 1; i < ns; i += step) c += abs(S(i) - S(i - W)); sum += c; cnt++; if (c < bc) { bc = c; best = W; } }
    return cnt && bc < 0.6 * (sum / cnt) ? best : 0;   // a clear dip at one lag = rows of that width
}
// record lanes: fixed-length records (tables, catalogues, spreadsheets) - the record length R is the lag (5..256)
// where bytes most often equal the byte R places before; 0 = none clearly better than other lags
static size_t g_rec = 0;
static size_t detect_record(const uint8_t *in, size_t n) {
    if (n < 65536) return 0; size_t step = max<size_t>(1, n / 65536); vector<double> f(257, 0); size_t cnt = 0;
    for (size_t i = 256; i < n; i += step) { cnt++; for (size_t R = 5; R <= 256; R++) f[R] += in[i] == in[i - R]; }
    size_t best = 0; double bv = 0, sum = 0; for (size_t R = 5; R <= 256; R++) { sum += f[R]; if (f[R] > bv * 1.0001) { bv = f[R]; best = R; } }
    // prefer the shortest lag that is nearly as good (a multiple of the true length scores about the same)
    for (size_t R = 5; R < best; R++) if (best % R == 0 && f[R] >= 0.97 * bv) { best = R; break; }
    return bv > 1.5 * (sum / 252) && bv / cnt > 0.2 ? best : 0;
}
static inline int med(int a, int b, int c) { int mx = max(a, b), mn = min(a, b); return c >= mx ? mn : c <= mn ? mx : a + b - c; }
static inline int act_line(int a, int b, int c, int d, int nl) { int g = abs(a - c) + abs(b - c) + abs(b - d);
    if (nl == 8) { int q = 0; while (g && q < 7) { g >>= 1; q++; } return q; }   // 8 lines: one per octave of activity
    if (g < 2) return g; int L = 31 - __builtin_clz(g); int q = 2 * L + ((g >> (L - 1)) & 1); return q > 15 ? 15 : q; }   // 16 lines: half octaves
static void actlines_enc(const uint8_t *in, size_t n, bool b16, size_t W, size_t blk, const char *cd2, B &t, int NL) {
    size_t bps = b16 ? 2 : 1, ns = n / bps; auto S = [&](size_t i) -> int { return b16 ? (in[2 * i] | (in[2 * i + 1] << 8)) : in[i]; };
    vector<B> lo(NL), hi(NL);
    for (size_t i = 0; i < ns; i++) { int a = i ? S(i - 1) : 0, b = i >= W ? S(i - W) : a, c = i > W ? S(i - W - 1) : a, d = i + 1 > W && i + 1 - W < ns ? S(i + 1 - W) : b;
        int pr = i > W ? med(a, b, c) : a, q = i > W ? act_line(a, b, c, d, NL) : 0;
        if (b16) { int16_t df = (int16_t)(S(i) - pr); uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); lo[q].push_back(z & 255); hi[q].push_back(z >> 8); }
        else { int8_t df = (int8_t)(S(i) - pr); lo[q].push_back((uint8_t)((df << 1) ^ (df >> 7))); } }
    put32(t, (uint32_t)W); t.push_back((uint8_t)NL);
    for (int q = 0; q < NL; q++) { put32(t, (uint32_t)lo[q].size());
        for (B *l : {&lo[q], &hi[q]}) { if (!b16 && l == &hi[q]) continue; size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, l->size()) : blk;
            for (size_t k = 0; k < l->size(); k += bl) enc_bytes(cd2, l->data() + k, min(bl, l->size() - k), t); } }
    t.insert(t.end(), in + ns * bps, in + n);
}
static size_t actlines_dec(const uint8_t *p, uint8_t *out, size_t n, bool b16, bool rn, size_t blk) {
    const uint8_t *p0 = p; size_t bps = b16 ? 2 : 1, ns = n / bps; size_t W = get32(p); p += 4; int NL = *p++; vector<B> lo(NL), hi(NL);
    for (int q = 0; q < NL; q++) { size_t cnt = get32(p); p += 4; lo[q].resize(cnt); if (b16) hi[q].resize(cnt);
        for (B *l : {&lo[q], &hi[q]}) { if (!b16 && l == &hi[q]) continue; size_t bl = rn ? max<size_t>(1, l->size()) : blk;
            for (size_t k = 0; k < l->size(); k += bl) p += dec_bytes(p, l->data() + k, min(bl, l->size() - k)); } }
    auto S = [&](size_t i) -> int { return b16 ? (out[2 * i] | (out[2 * i + 1] << 8)) : out[i]; };
    size_t pos[16] = {0};
    for (size_t i = 0; i < ns; i++) { int a = i ? S(i - 1) : 0, b = i >= W ? S(i - W) : a, c = i > W ? S(i - W - 1) : a, d = i + 1 > W && i + 1 - W < ns ? S(i + 1 - W) : b;
        int pr = i > W ? med(a, b, c) : a, q = i > W ? act_line(a, b, c, d, NL) : 0; size_t k = pos[q]++;
        if (b16) { uint16_t z = lo[q][k] | (hi[q][k] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1))); uint16_t v = (uint16_t)(pr + df); out[2 * i] = v & 255; out[2 * i + 1] = v >> 8; }
        else { uint8_t z = lo[q][k]; int8_t df = (int8_t)((z >> 1) ^ (uint8_t)(-(int)(z & 1))); out[i] = (uint8_t)(pr + df); } }
    memcpy(out + ns * bps, p, n - ns * bps); p += n - ns * bps; return p - p0;
}
// wasted bits (3 Nov 2026, as FLAC does): when every 16-bit sample of the segment is a multiple of 2^k (8-bit audio
// stored as 16-bit, for example), the audio and LPC lines work on the samples shifted down by k; k goes in the high
// four bits of the channel byte (0 in older files, so they still decode). The first 64 bytes (a file header, say) are
// not counted and are kept as they are next to the lines.
static const size_t WB_KEEP = 64;
static int wasted_bits(const uint8_t *in, size_t n) { unsigned o = 0; for (size_t i = WB_KEEP; i + 1 < n; i += 2) o |= in[i] | (in[i + 1] << 8); return o ? min(15, __builtin_ctz(o)) : 0; }
// audio lines: 16-bit samples (channel stride C = 1 mono, 2 stereo) predicted by a fixed polynomial of order K
// (1: a, 2: 2a-b, 3: 3a-3b+c, as in FLAC's fixed predictors); residuals sorted into 16 lines by the size of the
// previous two residuals of that channel, one table per line
static void audio_enc(const uint8_t *in, size_t n, int K, int C, size_t blk, const char *cd2, B &t) {
    size_t ns = n / 2; int wb = wasted_bits(in, n); auto S = [&](size_t i) -> int { return (int16_t)(in[2 * i] | (in[2 * i + 1] << 8)) >> wb; };
    vector<B> lo(16), hi(16); vector<int> res(ns, 0);
    for (size_t i = 0; i < ns; i++) { size_t c1 = i >= (size_t)C ? i - C : SIZE_MAX, c2 = i >= (size_t)2 * C ? i - 2 * C : SIZE_MAX, c3 = i >= (size_t)3 * C ? i - 3 * C : SIZE_MAX;
        int a = c1 != SIZE_MAX ? S(c1) : 0, b = c2 != SIZE_MAX ? S(c2) : a, c = c3 != SIZE_MAX ? S(c3) : b;
        int pr = K == 1 ? a : K == 2 ? 2 * a - b : 3 * a - 3 * b + c;
        int r1 = c1 != SIZE_MAX ? res[c1] : 0, r2 = c2 != SIZE_MAX ? res[c2] : 0; int g = abs(r1) + abs(r2), q;
        if (g < 2) q = g; else { int L = 31 - __builtin_clz(g); q = 2 * L + ((g >> (L - 1)) & 1); if (q > 15) q = 15; }
        int16_t df = (int16_t)(S(i) - pr); res[i] = df; uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); lo[q].push_back(z & 255); hi[q].push_back(z >> 8); }
    t.push_back((uint8_t)K); t.push_back((uint8_t)(C | (wb << 4))); if (wb) t.insert(t.end(), in, in + min(WB_KEEP, ns * 2));
    for (int q = 0; q < 16; q++) { put32(t, (uint32_t)lo[q].size());
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, l->size()) : blk;
            for (size_t k = 0; k < l->size(); k += bl) enc_bytes(cd2, l->data() + k, min(bl, l->size() - k), t); } }
    t.insert(t.end(), in + ns * 2, in + n);
}
static size_t audio_dec(const uint8_t *p, uint8_t *out, size_t n, bool rn, size_t blk) {
    const uint8_t *p0 = p; size_t ns = n / 2; int K = *p++, C = *p & 15, wb = *p >> 4; p++; const uint8_t *keep = p; if (wb) p += min(WB_KEEP, ns * 2); vector<B> lo(16), hi(16);
    for (int q = 0; q < 16; q++) { size_t cnt = get32(p); p += 4; lo[q].resize(cnt); hi[q].resize(cnt);
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = rn ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) p += dec_bytes(p, l->data() + k, min(bl, l->size() - k)); } }
    auto S = [&](size_t i) -> int { return (int16_t)(out[2 * i] | (out[2 * i + 1] << 8)) >> wb; };
    vector<int> res(ns, 0); size_t pos[16] = {0};
    for (size_t i = 0; i < ns; i++) { size_t c1 = i >= (size_t)C ? i - C : SIZE_MAX, c2 = i >= (size_t)2 * C ? i - 2 * C : SIZE_MAX, c3 = i >= (size_t)3 * C ? i - 3 * C : SIZE_MAX;
        int a = c1 != SIZE_MAX ? S(c1) : 0, b = c2 != SIZE_MAX ? S(c2) : a, c = c3 != SIZE_MAX ? S(c3) : b;
        int pr = K == 1 ? a : K == 2 ? 2 * a - b : 3 * a - 3 * b + c;
        int r1 = c1 != SIZE_MAX ? res[c1] : 0, r2 = c2 != SIZE_MAX ? res[c2] : 0; int g = abs(r1) + abs(r2), q;
        if (g < 2) q = g; else { int L = 31 - __builtin_clz(g); q = 2 * L + ((g >> (L - 1)) & 1); if (q > 15) q = 15; }
        size_t k = pos[q]++; uint16_t z = lo[q][k] | (hi[q][k] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1)));
        res[i] = df; uint16_t v = (uint16_t)((int)(int16_t)(pr + df) * (1 << wb)); out[2 * i] = v & 255; out[2 * i + 1] = v >> 8; }
    if (wb) memcpy(out, keep, min(WB_KEEP, ns * 2));
    memcpy(out + ns * 2, p, n - ns * 2); p += n - ns * 2; return p - p0;
}
// LPC audio lines: per block of 1024 samples (per channel, stride C) a linear predictor of order 2/4/8/12/16 is fitted
// (windowed autocorrelation + Levinson-Durbin, coefficients quantised to 13 fractional bits, as FLAC does in spirit),
// the order with the smallest residual cost is kept; residuals go to 16 context lines as in the audio lines above
static const int LPB = 1024, QSH = 13;
static void lpc_fit(const vector<double> &x, int P, vector<int> &q) {
    size_t N = x.size(); vector<double> w(N), R(P + 1, 0), a(P + 1, 0), tmp(P + 1);
    for (size_t i = 0; i < N; i++) w[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / N));
    for (int k = 0; k <= P; k++) for (size_t i = k; i < N; i++) R[k] += w[i] * w[i - k];
    R[0] *= 1.0 + 1e-9; q.assign(P, 0); if (R[0] <= 0) return;
    double e = R[0];
    for (int i = 1; i <= P; i++) { double acc = R[i]; for (int j = 1; j < i; j++) acc -= a[j] * R[i - j]; double k = acc / e;
        for (int j = 1; j < i; j++) tmp[j] = a[j] - k * a[i - j]; for (int j = 1; j < i; j++) a[j] = tmp[j]; a[i] = k; e *= (1 - k * k); if (e <= 0) break; }
    for (int j = 0; j < P; j++) { double v = a[j + 1] * (1 << QSH); q[j] = (int)max(-32768.0, min(32767.0, floor(v + 0.5))); }
}
static inline int lpc_pred(const int *h, const vector<int> &q) { long long acc = 0; for (size_t j = 0; j < q.size(); j++) acc += (long long)q[j] * h[-(int)j - 1]; return (int)(acc >> QSH); }
static void lpc_enc(const uint8_t *in, size_t n, int C, size_t blk, const char *cd2, B &t) {
    size_t ns = n / 2; int wb = wasted_bits(in, n); vector<int> S(ns); for (size_t i = 0; i < ns; i++) S[i] = (int16_t)(in[2 * i] | (in[2 * i + 1] << 8)) >> wb;
    vector<B> lo(16), hi(16); B head; head.push_back((uint8_t)(C | (wb << 4))); if (wb) head.insert(head.end(), in, in + min(WB_KEEP, ns * 2));
    for (int ch = 0; ch < C; ch++) {
        vector<int> x; for (size_t i = ch; i < ns; i += C) x.push_back(S[i]);
        vector<int> res(x.size(), 0);
        for (size_t b0 = 0; b0 < x.size(); b0 += LPB) { size_t b1 = min(x.size(), b0 + LPB); vector<double> xd(x.begin() + b0, x.begin() + b1);
            int bestP = 0; double bc = 1e300; vector<int> bq;
            for (int P : {2, 4, 8, 12, 16}) { if ((size_t)P * 4 > b1 - b0) continue; vector<int> q; lpc_fit(xd, P, q); double cst = 0;
                for (size_t i = b0; i < b1; i++) { int pr = i >= (size_t)P ? lpc_pred(&x[i], q) : (i ? x[i - 1] : 0); cst += log2(1.0 + abs(x[i] - pr)); }
                cst += P * 16; if (cst < bc) { bc = cst; bestP = P; bq = q; } }
            head.push_back((uint8_t)bestP); for (int v : bq) put16(head, (uint16_t)(int16_t)v);
            for (size_t i = b0; i < b1; i++) { int pr = (bestP && i >= (size_t)bestP) ? lpc_pred(&x[i], bq) : (i ? x[i - 1] : 0); int16_t df = (int16_t)(x[i] - pr); res[i] = df;
                int r1 = i ? res[i - 1] : 0, r2 = i > 1 ? res[i - 2] : 0; int g = abs(r1) + abs(r2), qq; if (g < 2) qq = g; else { int L = 31 - __builtin_clz(g); qq = 2 * L + ((g >> (L - 1)) & 1); if (qq > 15) qq = 15; }
                uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); lo[qq].push_back(z & 255); hi[qq].push_back(z >> 8); } } }
    put32(t, (uint32_t)head.size()); t.insert(t.end(), head.begin(), head.end());
    for (int q = 0; q < 16; q++) { put32(t, (uint32_t)lo[q].size());
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) enc_bytes(cd2, l->data() + k, min(bl, l->size() - k), t); } }
    t.insert(t.end(), in + ns * 2, in + n);
}
static size_t lpc_dec(const uint8_t *p, uint8_t *out, size_t n, bool rn, size_t blk) {
    const uint8_t *p0 = p; size_t ns = n / 2; size_t hl = get32(p); p += 4; const uint8_t *hp = p; p += hl; int C = *hp & 15, wb = *hp >> 4; hp++; const uint8_t *keep = hp; if (wb) hp += min(WB_KEEP, ns * 2);
    vector<B> lo(16), hi(16);
    for (int q = 0; q < 16; q++) { size_t cnt = get32(p); p += 4; lo[q].resize(cnt); hi[q].resize(cnt);
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = rn ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) p += dec_bytes(p, l->data() + k, min(bl, l->size() - k)); } }
    size_t pos[16] = {0};
    for (int ch = 0; ch < C; ch++) {
        size_t len = ns > (size_t)ch ? (ns - ch + C - 1) / C : 0; vector<int> x(len), res(len, 0);
        for (size_t b0 = 0; b0 < len; b0 += LPB) { size_t b1 = min(len, b0 + LPB); int P = *hp++; vector<int> q(P); for (int j = 0; j < P; j++) { q[j] = (int16_t)get16(hp); hp += 2; }
            for (size_t i = b0; i < b1; i++) { int pr = (P && i >= (size_t)P) ? lpc_pred(&x[i], q) : (i ? x[i - 1] : 0);
                int r1 = i ? res[i - 1] : 0, r2 = i > 1 ? res[i - 2] : 0; int g = abs(r1) + abs(r2), qq; if (g < 2) qq = g; else { int L = 31 - __builtin_clz(g); qq = 2 * L + ((g >> (L - 1)) & 1); if (qq > 15) qq = 15; }
                size_t k = pos[qq]++; uint16_t z = lo[qq][k] | (hi[qq][k] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1))); res[i] = df; x[i] = (int16_t)(pr + df); } }
        for (size_t i = 0, j = ch; i < len; i++, j += C) { uint16_t v = (uint16_t)(x[i] * (1 << wb)); out[2 * j] = v & 255; out[2 * j + 1] = v >> 8; } }
    if (wb) memcpy(out, keep, min(WB_KEEP, ns * 2));
    memcpy(out + ns * 2, p, n - ns * 2); p += n - ns * 2; return p - p0;
}

// STEREO LPC LINES (3 Nov 2026 evening): for 16-bit stereo, each block of LB2 frames picks one of four channel pairs, as
// FLAC does - left/right, left/side, side/right, mid/side (side = left - right, mid = (left + right) >> 1) - and an LPC
// order per channel from {4, 8, 12, 16, 24, 32}, coefficient precision chosen per block (no clipping). Every pair series is kept for the whole segment, so the predictor always
// has its own history across block borders. Residuals go to the same 16 context lines as the other audio lines.
// Wasted bits as in lpc_enc. head: C byte (2 | wb << 4), kept header bytes, block size (u16), per block: mode,
// then for each of the two series P, shift and P coefficients.
static const int LB2 = 4096;
static inline long long lpc_pred64(const long long *h, const vector<int> &q, int sh) { long long acc = 0; for (size_t j = 0; j < q.size(); j++) acc += (long long)q[j] * h[-(int)j - 1]; return acc >> sh; }
// as lpc_fit, but the coefficient precision (shift) is chosen per block so that no coefficient is clipped (FLAC does the same)
static int lpc_fit_sh(const vector<double> &x, int P, vector<int> &q) {
    size_t N = x.size(); vector<double> w(N), R(P + 1, 0), a(P + 1, 0), tmp(P + 1);
    for (size_t i = 0; i < N; i++) w[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / N));
    for (int k = 0; k <= P; k++) for (size_t i = k; i < N; i++) R[k] += w[i] * w[i - k];
    R[0] *= 1.0 + 1e-9; q.assign(P, 0); if (R[0] <= 0) return 12;
    double e = R[0];
    for (int i = 1; i <= P; i++) { double acc = R[i]; for (int j = 1; j < i; j++) acc -= a[j] * R[i - j]; double k = acc / e;
        for (int j = 1; j < i; j++) tmp[j] = a[j] - k * a[i - j]; for (int j = 1; j < i; j++) a[j] = tmp[j]; a[i] = k; e *= (1 - k * k); if (e <= 0) break; }
    double mx = 1e-9; for (int j = 1; j <= P; j++) mx = max(mx, fabs(a[j]));
    int sh = 15; while (sh > 0 && mx * (1 << sh) > 32767.0) sh--;
    for (int j = 0; j < P; j++) q[j] = (int)max(-32768.0, min(32767.0, floor(a[j + 1] * (1 << sh) + 0.5)));
    return sh;
}
static inline int ctxq(int r1, int r2) { int g = abs(r1) + abs(r2), qq; if (g < 2) qq = g; else { int L = 31 - __builtin_clz(g); qq = 2 * L + ((g >> (L - 1)) & 1); if (qq > 15) qq = 15; } return qq; }
static void lpc2_enc(const uint8_t *in, size_t n, size_t blk, const char *cd2, B &t) {
    size_t nf = n / 4; int wb = wasted_bits(in, n);
    // series: 0 = L, 1 = R, 2 = S = L - R, 3 = M = (L + R) >> 1   (all exact, as long long)
    vector<long long> X[4]; for (auto &v : X) v.resize(nf);
    for (size_t i = 0; i < nf; i++) { int L = (int16_t)(in[4 * i] | (in[4 * i + 1] << 8)) >> wb, R = (int16_t)(in[4 * i + 2] | (in[4 * i + 3] << 8)) >> wb;
        X[0][i] = L; X[1][i] = R; X[2][i] = (long long)L - R; X[3][i] = ((long long)L + R) >> 1; }
    static const int PAIR[4][2] = {{0, 1}, {0, 2}, {2, 1}, {3, 2}};
    vector<B> lo(16), hi(16); B head; head.push_back((uint8_t)(2 | (wb << 4))); if (wb) head.insert(head.end(), in, in + min(WB_KEEP, nf * 4)); put16(head, (uint16_t)LB2);
    int r1[2] = {0, 0}, r2[2] = {0, 0};
    for (size_t b0 = 0; b0 < nf; b0 += LB2) { size_t b1 = min(nf, b0 + LB2);
        // best order and coefficients per series for this block
        int bp[4], bs[4] = {12, 12, 12, 12}; vector<int> bq[4]; double bc[4];
        for (int sidx = 0; sidx < 4; sidx++) { vector<double> xd(X[sidx].begin() + b0, X[sidx].begin() + b1); bc[sidx] = 1e300; bp[sidx] = 0;
            for (int P : {4, 8, 12, 16, 24, 32}) { if ((size_t)P * 4 > b1 - b0) continue; vector<int> q; int sh = lpc_fit_sh(xd, P, q); double c = 0;
                for (size_t i = b0; i < b1; i++) { long long pr = i >= (size_t)P ? lpc_pred64(&X[sidx][i], q, sh) : (i ? X[sidx][i - 1] : 0); c += log2(1.0 + (double)llabs(X[sidx][i] - pr)); }
                c += P * 16; if (c < bc[sidx]) { bc[sidx] = c; bp[sidx] = P; bq[sidx] = q; bs[sidx] = sh; } } }
        bool msok = true; for (size_t i = b0; i < b1 && msok; i++) if (llabs(X[2][i]) > 32767) msok = false;
        int mode = 0; double best = 1e300;
        for (int m = 0; m < 4; m++) { if (m == 3 && !msok) continue; double c = bc[PAIR[m][0]] + bc[PAIR[m][1]]; if (c < best) { best = c; mode = m; } }
        head.push_back((uint8_t)mode);
        for (int k = 0; k < 2; k++) { int sidx = PAIR[mode][k]; head.push_back((uint8_t)bp[sidx]); head.push_back((uint8_t)bs[sidx]); for (int v : bq[sidx]) put16(head, (uint16_t)(int16_t)v); }
        for (size_t i = b0; i < b1; i++) for (int k = 0; k < 2; k++) { int sidx = PAIR[mode][k], P = bp[sidx];
            long long pr = (P && i >= (size_t)P) ? lpc_pred64(&X[sidx][i], bq[sidx], bs[sidx]) : (i ? X[sidx][i - 1] : 0);
            int16_t df = (int16_t)(X[sidx][i] - pr); int qq = ctxq(r1[k], r2[k]); r2[k] = r1[k]; r1[k] = df;
            uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); lo[qq].push_back(z & 255); hi[qq].push_back(z >> 8); } }
    put32(t, (uint32_t)head.size()); t.insert(t.end(), head.begin(), head.end());
    for (int q = 0; q < 16; q++) { put32(t, (uint32_t)lo[q].size());
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) enc_bytes(cd2, l->data() + k, min(bl, l->size() - k), t); } }
    t.insert(t.end(), in + nf * 4, in + n);
}
static size_t lpc2_dec(const uint8_t *p, uint8_t *out, size_t n, bool rn, size_t blk) {
    const uint8_t *p0 = p; size_t nf = n / 4; size_t hl = get32(p); p += 4; const uint8_t *hp = p; p += hl;
    int wb = *hp >> 4; hp++; const uint8_t *keep = hp; if (wb) hp += min(WB_KEEP, nf * 4); size_t LB = get16(hp); hp += 2;
    vector<B> lo(16), hi(16);
    for (int q = 0; q < 16; q++) { size_t cnt = get32(p); p += 4; lo[q].resize(cnt); hi[q].resize(cnt);
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = rn ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) p += dec_bytes(p, l->data() + k, min(bl, l->size() - k)); } }
    static const int PAIR[4][2] = {{0, 1}, {0, 2}, {2, 1}, {3, 2}};
    vector<long long> X[4]; for (auto &v : X) v.resize(nf);
    size_t pos[16] = {0}; int r1[2] = {0, 0}, r2[2] = {0, 0};
    for (size_t b0 = 0; b0 < nf; b0 += LB) { size_t b1 = min(nf, b0 + LB); int mode = *hp++; int P[2], sh[2]; vector<int> q[2];
        for (int k = 0; k < 2; k++) { P[k] = *hp++; sh[k] = *hp++; q[k].resize(P[k]); for (int j = 0; j < P[k]; j++) { q[k][j] = (int16_t)get16(hp); hp += 2; } }
        for (size_t i = b0; i < b1; i++) { long long v[2];
            for (int k = 0; k < 2; k++) { int sidx = PAIR[mode][k];
                long long pr = (P[k] && i >= (size_t)P[k]) ? lpc_pred64(&X[sidx][i], q[k], sh[k]) : (i ? X[sidx][i - 1] : 0);
                int qq = ctxq(r1[k], r2[k]); size_t kk = pos[qq]++; uint16_t z = lo[qq][kk] | (hi[qq][kk] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1)));
                r2[k] = r1[k]; r1[k] = df; v[k] = (long long)(int16_t)(pr + df); }   // value mod 2^16 (exact for L, R, M; side may wrap)
            long long L, R;
            if (mode == 0) { L = v[0]; R = v[1]; }
            else if (mode == 1) { L = v[0]; R = (int16_t)(L - v[1]); }
            else if (mode == 2) { R = v[1]; L = (int16_t)(R + v[0]); }
            else { long long m = v[0], sd = v[1]; m = (m << 1) | (sd & 1); L = (m + sd) >> 1; R = (m - sd) >> 1; }
            X[0][i] = L; X[1][i] = R; X[2][i] = L - R; X[3][i] = (L + R) >> 1;
            uint16_t a = (uint16_t)(L * (1 << wb)), c = (uint16_t)(R * (1 << wb)); out[4 * i] = a & 255; out[4 * i + 1] = a >> 8; out[4 * i + 2] = c & 255; out[4 * i + 3] = c >> 8; } }
    if (wb) memcpy(out, keep, min(WB_KEEP, nf * 4));
    memcpy(out + nf * 4, p, n - nf * 4); p += n - nf * 4; return p - p0;
}

// MONO LPC LINES v2 (3 Nov 2026 evening): as the stereo lines (orders 4-32, precision per block, no clipping) for one
// channel; block size LB (1024 or 4096 frames) in the header. ids 58/59 = 4096-sample blocks, 60/61 = 1024 (FSE / rANS).
static void lpc1_enc(const uint8_t *in, size_t n, size_t LB, size_t blk, const char *cd2, B &t) {
    size_t ns = n / 2; int wb = wasted_bits(in, n); vector<long long> X(ns);
    for (size_t i = 0; i < ns; i++) X[i] = (int16_t)(in[2 * i] | (in[2 * i + 1] << 8)) >> wb;
    vector<B> lo(16), hi(16); B head; head.push_back((uint8_t)(1 | (wb << 4))); if (wb) head.insert(head.end(), in, in + min(WB_KEEP, ns * 2)); put16(head, (uint16_t)LB);
    int r1 = 0, r2 = 0;
    for (size_t b0 = 0; b0 < ns; b0 += LB) { size_t b1 = min(ns, b0 + LB); vector<double> xd(X.begin() + b0, X.begin() + b1);
        int bp = 0, bs = 12; vector<int> bq; double bc = 1e300;
        for (int P : {2, 4, 8, 12, 16, 24, 32}) { if ((size_t)P * 4 > b1 - b0) continue; vector<int> q; int sh = lpc_fit_sh(xd, P, q); double c = 0;
            for (size_t i = b0; i < b1; i++) { long long pr = i >= (size_t)P ? lpc_pred64(&X[i], q, sh) : (i ? X[i - 1] : 0); c += log2(1.0 + (double)llabs(X[i] - pr)); }
            c += P * 16; if (c < bc) { bc = c; bp = P; bq = q; bs = sh; } }
        head.push_back((uint8_t)bp); head.push_back((uint8_t)bs); for (int v : bq) put16(head, (uint16_t)(int16_t)v);
        for (size_t i = b0; i < b1; i++) { long long pr = (bp && i >= (size_t)bp) ? lpc_pred64(&X[i], bq, bs) : (i ? X[i - 1] : 0);
            int16_t df = (int16_t)(X[i] - pr); int qq = ctxq(r1, r2); r2 = r1; r1 = df;
            uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); lo[qq].push_back(z & 255); hi[qq].push_back(z >> 8); } }
    put32(t, (uint32_t)head.size()); t.insert(t.end(), head.begin(), head.end());
    for (int q = 0; q < 16; q++) { put32(t, (uint32_t)lo[q].size());
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) enc_bytes(cd2, l->data() + k, min(bl, l->size() - k), t); } }
    t.insert(t.end(), in + ns * 2, in + n);
}
static size_t lpc1_dec(const uint8_t *p, uint8_t *out, size_t n, bool rn, size_t blk) {
    const uint8_t *p0 = p; size_t ns = n / 2; size_t hl = get32(p); p += 4; const uint8_t *hp = p; p += hl;
    int wb = *hp >> 4; hp++; const uint8_t *keep = hp; if (wb) hp += min(WB_KEEP, ns * 2); size_t LB = get16(hp); hp += 2;
    vector<B> lo(16), hi(16);
    for (int q = 0; q < 16; q++) { size_t cnt = get32(p); p += 4; lo[q].resize(cnt); hi[q].resize(cnt);
        for (B *l : {&lo[q], &hi[q]}) { size_t bl = rn ? max<size_t>(1, l->size()) : blk; for (size_t k = 0; k < l->size(); k += bl) p += dec_bytes(p, l->data() + k, min(bl, l->size() - k)); } }
    vector<long long> X(ns); size_t pos[16] = {0}; int r1 = 0, r2 = 0;
    for (size_t b0 = 0; b0 < ns; b0 += LB) { size_t b1 = min(ns, b0 + LB); int P = *hp++, sh = *hp++; vector<int> q(P); for (int j = 0; j < P; j++) { q[j] = (int16_t)get16(hp); hp += 2; }
        for (size_t i = b0; i < b1; i++) { long long pr = (P && i >= (size_t)P) ? lpc_pred64(&X[i], q, sh) : (i ? X[i - 1] : 0);
            int qq = ctxq(r1, r2); size_t kk = pos[qq]++; uint16_t z = lo[qq][kk] | (hi[qq][kk] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1)));
            r2 = r1; r1 = df; X[i] = (int16_t)(pr + df); uint16_t v = (uint16_t)(X[i] * (1 << wb)); out[2 * i] = v & 255; out[2 * i + 1] = v >> 8; } }
    if (wb) memcpy(out, keep, min(WB_KEEP, ns * 2));
    memcpy(out + ns * 2, p, n - ns * 2); p += n - ns * 2; return p - p0;
}

// RGB COLOUR LINES (3 Nov 2026 evening): 8-bit pixels of 3 bytes (RGB or BGR - green is the middle byte). Green is
// predicted from the green of the left, upper and upper-left pixels (median predictor); red and blue are first taken
// as differences from their own pixel's green (the "subtract green" step of WebP / JPEG XL) and predicted the same way.
// Residuals go to 16 activity lines per plane (activity = |a-c| + |b-c| + |b-d| of that plane's neighbours).
// The row width (pixels) and the phase (where the first whole pixel starts, 0..2) are found by the encoder.
// head: phase (1), width in pixels (u32). Bytes before the phase and after the last whole pixel are kept as they are.
static size_t rgb_width(const uint8_t *in, size_t n) {   // row length in bytes, a multiple of 3, by the dip in |x[i] - x[i-W]|
    if (n < 65536) return 0; size_t step = max<size_t>(1, n / 16384), best = 0; double bc = 1e300, sum = 0; size_t cnt = 0;
    for (size_t W = 24; W <= 3 * 8192 && W < n / 4; W += 3) { double c = 0; for (size_t i = W + 8; i < n; i += step) c += abs((int)in[i] - (int)in[i - W]); sum += c; cnt++; if (c < bc) { bc = c; best = W; } }
    return cnt && bc < 0.7 * (sum / cnt) ? best : 0;
}
static int g_gap = 0;
static inline int actq(int g) { if (g < 2) return g; int L = 31 - __builtin_clz(g); int q = 2 * L + ((g >> (L - 1)) & 1); return q > 15 ? 15 : q; }
static void rgb_core(const uint8_t *in, size_t n, size_t ph, size_t Wp, vector<B> &lines, bool enc, uint8_t *out, const vector<B> *lin, size_t *pos) {
    size_t np = (n - ph) / 3; const uint8_t *src = enc ? in : out;
    auto V = [&](size_t pix, int pl) -> int {   // plane value: G, or R-G / B-G as signed differences (-255..255)
        const uint8_t *q = src + ph + 3 * pix; return pl == 1 ? (int)q[1] : (int)q[pl] - (int)q[1]; };
    for (size_t pix = 0; pix < np; pix++) { size_t x = pix % Wp;
        for (int pl : {1, 0, 2}) {
            int a = x ? V(pix - 1, pl) : (pix >= Wp ? V(pix - Wp, pl) : 0);
            int b = pix >= Wp ? V(pix - Wp, pl) : a;
            int c = (pix >= Wp && x) ? V(pix - Wp - 1, pl) : b;
            int d = (pix >= Wp && x + 1 < Wp) ? V(pix - Wp + 1, pl) : b;
            int pr;
            if (g_gap && pix >= 2 * Wp && x >= 2 && x + 1 < Wp) {   // CALIC's gradient-adjusted predictor (GAP)
                int ww = V(pix - 2, pl), nn = V(pix - 2 * Wp, pl), nne = V(pix - 2 * Wp + 1, pl);
                int dh = abs(a - ww) + abs(b - c) + abs(b - d), dv = abs(a - c) + abs(b - nn) + abs(d - nne);
                if (dv - dh > 80) pr = a; else if (dh - dv > 80) pr = b;
                else { int p2 = (a + b) * 4 + (d - c) * 2;   // in eighths: (a+b)/2 + (d-c)/4
                    if (dv - dh > 32) p2 = (p2 + 8 * a) / 2; else if (dv - dh > 8) p2 = (3 * p2 + 8 * a) / 4;
                    else if (dh - dv > 32) p2 = (p2 + 8 * b) / 2; else if (dh - dv > 8) p2 = (3 * p2 + 8 * b) / 4;
                    pr = (p2 >= 0 ? p2 + 4 : p2 - 3) / 8; }
            } else pr = med(a, b, c); int q = actq(abs(a - c) + abs(b - c) + abs(b - d)) + 16 * (pl == 1 ? 0 : pl == 0 ? 1 : 2);
            if (enc) { int8_t df = (int8_t)(V(pix, pl) - pr); lines[q].push_back((uint8_t)((df << 1) ^ (df >> 7))); }
            else { uint8_t z = (*lin)[q][pos[q]++]; int8_t df = (int8_t)((z >> 1) ^ (uint8_t)(-(int)(z & 1))); uint8_t v = (uint8_t)(pr + df);
                uint8_t *o = out + ph + 3 * pix; if (pl == 1) o[1] = v; else o[pl] = (uint8_t)(pr + df + o[1]); } } }
}
static void rgb_enc(const uint8_t *in, size_t n, size_t blk, const char *cd2, B &t) {
    size_t W = rgb_width(in, n); if (!W) { t.clear(); t.resize(n + 64, 0); return; }
    size_t Wp = W / 3; B best; 
    for (int gap = 0; gap < 2; gap++) for (size_t ph = 0; ph < 3; ph++) { g_gap = gap; vector<B> L(48); rgb_core(in, n, ph, Wp, L, true, nullptr, nullptr, nullptr);
        B c; c.push_back((uint8_t)(ph | (gap << 7))); put32(c, (uint32_t)Wp);
        for (int q = 0; q < 48; q++) { put32(c, (uint32_t)L[q].size()); size_t bl = strcmp(cd2, "rans") == 0 ? max<size_t>(1, L[q].size()) : blk;
            for (size_t k = 0; k < L[q].size(); k += bl) enc_bytes(cd2, L[q].data() + k, min(bl, L[q].size() - k), c); }
        size_t np = (n - ph) / 3; c.insert(c.end(), in, in + ph); c.insert(c.end(), in + ph + 3 * np, in + n);
        if (best.empty() || c.size() < best.size()) best.swap(c); }
    t.insert(t.end(), best.begin(), best.end());
}
static size_t rgb_dec(const uint8_t *p, uint8_t *out, size_t n, bool rn, size_t blk) {
    const uint8_t *p0 = p; int gap = *p >> 7; size_t ph = *p++ & 127; size_t Wp = get32(p); p += 4; vector<B> L(48);
    for (int q = 0; q < 48; q++) { size_t cnt = get32(p); p += 4; L[q].resize(cnt); size_t bl = rn ? max<size_t>(1, cnt) : blk;
        for (size_t k = 0; k < cnt; k += bl) p += dec_bytes(p, L[q].data() + k, min(bl, cnt - k)); }
    size_t pos[48] = {0}; vector<B> dummy; g_gap = gap; rgb_core(nullptr, n, ph, Wp, dummy, false, out, &L, pos);
    size_t np = (n - ph) / 3; memcpy(out, p, ph); p += ph; memcpy(out + ph + 3 * np, p, n - ph - 3 * np); p += n - ph - 3 * np; return p - p0;
}
static void lanes_enc(const uint8_t *in, size_t n, int L, size_t blk, B &o, const char *coder) {
    for (int l = 0; l < L; l++) { B lane; for (size_t i = l; i < n; i += L) lane.push_back(in[i]);
        size_t bl = strcmp(coder, "rans") == 0 ? lane.size() : blk;   // rANS: one table per lane per segment
        for (size_t q = 0; q < lane.size(); q += bl) enc_bytes(coder, lane.data() + q, min(bl, lane.size() - q), o); }
}
// ids: 0/2/3/4 lanes by Huff0, +32 lanes by FSE (table 12), +128 lanes by rANS; 12 = 12-bit samples by rANS;
// 13/14 = 12-bit samples re-centred into one byte each (rare outliers kept aside) by Huff0 / FSE; 64/65/66 = two context lines by rANS/FSE/Huff0
static const int HX_NC = 58; static const int HX_IDS[HX_NC] = {56, 57, 58, 59, 60, 61, 62, 63, 0, 2, 3, 4, 12, 128 + 0, 128 + 2, 128 + 3, 128 + 4, 64, 32 + 0, 32 + 2, 32 + 3, 32 + 4, 13, 65, 66, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55};
// 52-55 = LPC audio lines (52/53 mono, 54/55 stereo; FSE / rANS)
// 44-51 = audio lines (16-bit; even = FSE, odd = rANS): 44/45 order 2 mono, 46/47 order 3 mono, 48/49 order 2 stereo, 50/51 order 3 stereo
// 40/41 = record lanes (one lane per byte position in an R-byte record) by FSE / rANS; 42/43 = record lanes as differences from the same byte of the record before
// 28-31 = as 24-27 with 16 activity lines (half octaves) instead of 8
// 24/25 (16-bit) and 26/27 (8-bit) = image lines PLUS activity lines: residuals sorted into 8 lines by how busy the
// neighbourhood is (|a-c| + |b-c| + |b-d|), one table per line, FSE / rANS
// 20/21 = 16-bit image lines: each sample predicted from left, above and upper-left (median predictor), residual by FSE / rANS; 22/23 = the same for 8-bit samples
// 16/17 = 16-bit little-endian samples as zig-zag differences from the sample before, two byte lanes by FSE / rANS;
// 18/19 = bytes as zig-zag differences from the byte before, by FSE / rANS (delta lines for sampled data: sound, images, sensors)
static void hx_cand(int id, const uint8_t *in, size_t n, size_t blk, B &t) {
    t.push_back((uint8_t)id); int L = id & 31; const char *cd = id & 128 ? "rans" : (id & 32) ? "fse12" : "huf";
        if (id == 62 || id == 63) { rgb_enc(in, n, blk, (id & 1) ? "rans" : "fse12", t); return; }
        if (id >= 58 && id <= 61) { lpc1_enc(in, n, id <= 59 ? 4096 : 1024, blk, (id & 1) ? "rans" : "fse12", t); return; }
        if (id == 56 || id == 57) { lpc2_enc(in, n, blk, (id & 1) ? "rans" : "fse12", t); return; }
        if (id >= 52 && id <= 55) { lpc_enc(in, n, id >= 54 ? 2 : 1, blk, (id & 1) ? "rans" : "fse12", t); return; }
        if (id >= 44 && id <= 51) { int k = id - 44; audio_enc(in, n, (k & 2) ? 3 : 2, k >= 4 ? 2 : 1, blk, (id & 1) ? "rans" : "fse12", t); return; }
        if (id >= 40 && id <= 43) { size_t R = g_rec; if (!R) { t.clear(); t.resize(n + 64, 0); return; }
            const char *cd2 = (id & 1) ? "rans" : "fse12"; put32(t, (uint32_t)R); B d(in, in + n);
            if (id >= 42) for (size_t i = n; i-- > R;) d[i] = (uint8_t)(in[i] - in[i - R]);
            for (size_t l = 0; l < R; l++) { B lane; for (size_t i = l; i < n; i += R) lane.push_back(d[i]);
                size_t bl = (id & 1) ? max<size_t>(1, lane.size()) : blk; for (size_t q = 0; q < lane.size(); q += bl) enc_bytes(cd2, lane.data() + q, min(bl, lane.size() - q), t); }
            return; }
        if (id >= 24 && id <= 31) { int id2 = id >= 28 ? id - 4 : id; bool b16 = id2 <= 25; size_t W = b16 ? g_w16 : g_w8; if (!W) { t.clear(); t.resize(n + 64, 0); return; }
            actlines_enc(in, n, b16, W, blk, (id & 1) ? "rans" : "fse12", t, id >= 28 ? 16 : 8); return; }
        if (id >= 20 && id <= 23) { bool b16 = id <= 21; size_t W = b16 ? g_w16 : g_w8; if (!W) { t.clear(); t.resize(n + 64, 0); return; }   // not an image: never chosen
            const char *cd2 = (id & 1) ? "rans" : "fse12"; put32(t, (uint32_t)W); B d(n);
            if (b16) { size_t ns = n / 2; auto S = [&](size_t i) -> int { return in[2 * i] | (in[2 * i + 1] << 8); };
                for (size_t i = 0; i < ns; i++) { int a = i ? S(i - 1) : 0, pr = i > W ? med(a, S(i - W), S(i - W - 1)) : a; int16_t df = (int16_t)(S(i) - pr);
                    uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); d[2 * i] = z & 255; d[2 * i + 1] = z >> 8; }
                if (n & 1) d[n - 1] = in[n - 1]; lanes_enc(d.data(), n, 2, blk, t, cd2); }
            else { for (size_t i = 0; i < n; i++) { int a = i ? in[i - 1] : 0, pr = i > W ? med(a, in[i - W], in[i - W - 1]) : a; int8_t df = (int8_t)(in[i] - pr); d[i] = (uint8_t)((df << 1) ^ (df >> 7)); }
                lanes_enc(d.data(), n, 1, blk, t, cd2); }
            return; }
        if (id >= 16 && id <= 19) { const char *cd2 = (id & 1) ? "rans" : "fse12"; B d(n);
            if (id <= 17) { uint16_t prev = 0; size_t ns = n / 2;
                for (size_t j = 0; j < ns; j++) { uint16_t v = in[2 * j] | (in[2 * j + 1] << 8); int16_t df = (int16_t)(v - prev); uint16_t z = (uint16_t)((df << 1) ^ (df >> 15)); d[2 * j] = z & 255; d[2 * j + 1] = z >> 8; prev = v; }
                if (n & 1) d[n - 1] = in[n - 1];
                lanes_enc(d.data(), n, 2, blk, t, cd2); }
            else { uint8_t prev = 0; for (size_t i = 0; i < n; i++) { int8_t df = (int8_t)(in[i] - prev); d[i] = (uint8_t)((df << 1) ^ (df >> 7)); prev = in[i]; }
                lanes_enc(d.data(), n, 1, blk, t, cd2); }
            return; }
        if (id == 13 || id == 14) { size_t ns = n / 3 * 2; vector<uint16_t> sm(ns); vector<uint32_t> hist(4096, 0);
            for (size_t i = 0, j = 0; j < ns; i += 3, j += 2) { sm[j] = in[i] | ((in[i + 1] & 15) << 8); sm[j + 1] = (in[i + 1] >> 4) | (in[i + 2] << 4); hist[sm[j]]++; hist[sm[j + 1]]++; }
            int mode = (int)(max_element(hist.begin(), hist.end()) - hist.begin()); B by(ns); vector<uint16_t> exc;
            for (size_t j = 0; j < ns; j++) { int b = (int)sm[j] - mode + 128; if (b >= 0 && b < 255) by[j] = (uint8_t)b; else { by[j] = 255; exc.push_back(sm[j]); } }
            put16(t, mode); put32(t, (uint32_t)exc.size()); for (auto e : exc) put16(t, e);
            for (size_t q = 0; q < ns; q += blk) enc_bytes(id == 13 ? "huf" : "fse12", by.data() + q, min(blk, ns - q), t);
            t.insert(t.end(), in + ns / 2 * 3, in + n); return; }
        if (id == 65 || id == 66) { B l0, l1; int prev = 0; for (size_t i = 0; i < n; i++) { (prev & 1 ? l1 : l0).push_back(in[i]); prev = in[i]; }
            const char *cd2 = id == 65 ? "fse12" : "huf"; put32(t, (uint32_t)l0.size());
            for (B *l : {&l0, &l1}) for (size_t q = 0; q < l->size(); q += blk) enc_bytes(cd2, l->data() + q, min(blk, l->size() - q), t);
            return; }
        if (id == 64) {   // context lines: each byte goes to line 0 or 1 by the low bit of the byte before it, one rANS table per line
            vector<uint16_t> s0, s1; int prev = 0; for (size_t i = 0; i < n; i++) { (prev & 1 ? s1 : s0).push_back(in[i]); prev = in[i]; }
            put32(t, (uint32_t)s0.size()); rans_enc(s0.data(), s0.size(), 256, t); rans_enc(s1.data(), s1.size(), 256, t); return; }
        if (L == 0) lanes_enc(in, n, 1, blk, t, cd);
        else if (L <= 4) lanes_enc(in, n, L, blk, t, cd);
        else { size_t ns = n / 3 * 2; vector<uint16_t> s(ns);
            for (size_t i = 0, j = 0; j < ns; i += 3, j += 2) { s[j] = in[i] | ((in[i + 1] & 15) << 8); s[j + 1] = (in[i + 1] >> 4) | (in[i + 2] << 4); }
            rans_enc(s.data(), ns, 4096, t); t.insert(t.end(), in + ns / 2 * 3, in + n); }   // tail bytes stored as they are
}
static int hx_rank(int i) { if ((i >= 16 && i <= 31) || (i >= 40 && i <= 63)) return (i & 1) ? 2 : 1; if (i == 12 || i == 64) return 3; if (i == 13 || i == 66) return 0; if (i == 14 || i == 65) return 1;
                            return i & 128 ? 2 : i & 32 ? 1 : 0; }   // decode speed: Huff0 0, FSE 1, rANS 2, 12-bit / context rANS 3
// HX Fast 8 speed-ups (3 Nov 2026): g_ct = threads per segment for the candidates (used when there are fewer segments
// than cores); g_probe = probe size: every line is first tried on a probe slice from the middle of the segment, and only
// the lines within 3% of the best probe (at most 6) are run on the whole segment. 0 = try every line in full (as before).
static int g_ct = 1; static size_t g_probe = 0; static int g_slices = 1;
static void hx_run(const vector<int> &which, const uint8_t *in, size_t n, size_t blk, vector<B> &cand) {
    auto one = [&](int c) { cand[c].clear(); hx_cand(HX_IDS[c], in, n, blk, cand[c]); };
    int T = min<int>(g_ct, (int)which.size());
    if (T <= 1) { for (int c : which) one(c); return; }
    std::atomic<size_t> nx(0); vector<thread> th;
    for (int t = 0; t < T; t++) th.emplace_back([&]() { for (size_t k; (k = nx++) < which.size();) one(which[k]); });
    for (auto &x : th) x.join();
}
static size_t gcd_z(size_t a, size_t b) { while (b) { size_t t = a % b; a = b; b = t; } return a; }
static int hx_seg_enc(const uint8_t *in, size_t n, size_t blk, B &o, const vector<int> *allow = nullptr, vector<int> *kept = nullptr) {
    vector<B> cand(HX_NC); vector<int> which;
    if (allow) { for (int c = 0; c < HX_NC; c++) if (find(allow->begin(), allow->end(), HX_IDS[c]) != allow->end()) which.push_back(c); }
    if (which.empty() && g_probe && n >= 4 * g_probe) {
        size_t A = 12; if (g_rec) { size_t l = 12 / gcd_z(12, g_rec) * g_rec; A = l <= n / 8 ? l : (g_rec <= n / 8 ? g_rec : 12); }
        // probe slices: g_slices slices of g_probe bytes each, spread evenly over the segment; sizes added up per line
        size_t L = max(A, g_probe / A * A); vector<int> all(HX_NC); for (int c = 0; c < HX_NC; c++) all[c] = c; vector<size_t> ps(HX_NC, 0);
        for (int sl = 0; sl < g_slices; sl++) { size_t mid = n * (2 * sl + 1) / (2 * g_slices), st = (mid - min(mid, L / 2)) / A * A; if (st + L > n) st = (n - L) / A * A;
            vector<B> pc(HX_NC); hx_run(all, in + st, L, blk, pc); for (int c = 0; c < HX_NC; c++) ps[c] += pc[c].size(); }
        size_t smin = *min_element(ps.begin(), ps.end());
        vector<int> ord = all; stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return ps[x] < ps[y]; });
        for (int c : ord) if (ps[c] <= smin + smin * 3 / 100 && which.size() < 6) which.push_back(c);
    }
    if (which.empty()) for (int c = 0; c < HX_NC; c++) which.push_back(c);
    hx_run(which, in, n, blk, cand);
    if (kept) for (int c : which) kept->push_back(HX_IDS[c]);
    // speed first (Michael's rule): the smallest wins, but a faster decoder within 0.5% of it is taken instead
    size_t mn = SIZE_MAX; for (int c : which) mn = min(mn, cand[c].size());
    int best = -1;
    for (int c : which) if (cand[c].size() <= mn + mn / 200)
        if (best < 0 || hx_rank(HX_IDS[c]) < hx_rank(HX_IDS[best]) || (hx_rank(HX_IDS[c]) == hx_rank(HX_IDS[best]) && cand[c].size() < cand[best].size())) best = c;
    if (getenv("HX_DEBUG")) { string w; for (int c : which) w += to_string(HX_IDS[c]) + ":" + to_string(cand[c].size()) + " "; fprintf(stderr, "seg %zu best %d | %s\n", n, HX_IDS[best], w.c_str()); }
    o.insert(o.end(), cand[best].begin(), cand[best].end());
    return HX_IDS[best];
}
static size_t hx_seg_dec(const uint8_t *p, uint8_t *out, size_t n, size_t blk) {
    const uint8_t *p0 = p; int id = *p++;
    if (id == 62 || id == 63) { p += rgb_dec(p, out, n, id & 1, blk); return p - p0; }
    if (id >= 58 && id <= 61) { p += lpc1_dec(p, out, n, id & 1, blk); return p - p0; }
    if (id == 56 || id == 57) { p += lpc2_dec(p, out, n, id & 1, blk); return p - p0; }
    if (id >= 52 && id <= 55) { p += lpc_dec(p, out, n, id & 1, blk); return p - p0; }
    if (id >= 44 && id <= 51) { p += audio_dec(p, out, n, id & 1, blk); return p - p0; }
    if (id >= 40 && id <= 43) { size_t R = get32(p); p += 4; bool rn = id & 1; B lane;
        for (size_t l = 0; l < R; l++) { size_t ln = (n > l) ? (n - l + R - 1) / R : 0; lane.resize(ln); size_t bl = rn ? max<size_t>(1, ln) : blk;
            for (size_t q = 0; q < ln; q += bl) p += dec_bytes(p, lane.data() + q, min(bl, ln - q));
            for (size_t i = l, k = 0; i < n; i += R, k++) out[i] = lane[k]; }
        if (id >= 42) for (size_t i = R; i < n; i++) out[i] = (uint8_t)(out[i] + out[i - R]);
        return p - p0; }
    if (id >= 24 && id <= 31) { int b = id >= 28 ? id - 4 : id; p += actlines_dec(p, out, n, b <= 25, id & 1, blk); return p - p0; }
    if (id >= 20 && id <= 23) { bool b16 = id <= 21, rn = id & 1; size_t W = get32(p); p += 4; B d(n); size_t L = b16 ? 2 : 1;
        { B lane; for (size_t l = 0; l < L; l++) { size_t ln = (n - l + L - 1) / L; lane.resize(ln); size_t bl = rn ? ln : blk;
            for (size_t q = 0; q < ln; q += bl) p += dec_bytes(p, lane.data() + q, min(bl, ln - q));
            for (size_t i = l, k = 0; i < n; i += L, k++) d[i] = lane[k]; } }
        if (b16) { size_t ns = n / 2; auto S = [&](size_t i) -> int { return out[2 * i] | (out[2 * i + 1] << 8); };
            for (size_t i = 0; i < ns; i++) { uint16_t z = d[2 * i] | (d[2 * i + 1] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1)));
                int a = i ? S(i - 1) : 0, pr = i > W ? med(a, S(i - W), S(i - W - 1)) : a; uint16_t v = (uint16_t)(pr + df); out[2 * i] = v & 255; out[2 * i + 1] = v >> 8; }
            if (n & 1) out[n - 1] = d[n - 1]; }
        else for (size_t i = 0; i < n; i++) { uint8_t z = d[i]; int8_t df = (int8_t)((z >> 1) ^ (uint8_t)(-(int)(z & 1)));
            int a = i ? out[i - 1] : 0, pr = i > W ? med(a, out[i - W], out[i - W - 1]) : a; out[i] = (uint8_t)(pr + df); }
        return p - p0; }
    if (id >= 16 && id <= 19) { B d(n); bool rn = id & 1; size_t L = id <= 17 ? 2 : 1;
        { B lane; for (size_t l = 0; l < L; l++) { size_t ln = (n - l + L - 1) / L; lane.resize(ln); size_t bl = rn ? ln : blk;
            for (size_t q = 0; q < ln; q += bl) p += dec_bytes(p, lane.data() + q, min(bl, ln - q));
            for (size_t i = l, k = 0; i < n; i += L, k++) d[i] = lane[k]; } }
        if (id <= 17) { uint16_t prev = 0; size_t ns = n / 2;
            for (size_t j = 0; j < ns; j++) { uint16_t z = d[2 * j] | (d[2 * j + 1] << 8); int16_t df = (int16_t)((z >> 1) ^ (uint16_t)(-(int)(z & 1))); uint16_t v = (uint16_t)(prev + df); out[2 * j] = v & 255; out[2 * j + 1] = v >> 8; prev = v; }
            if (n & 1) out[n - 1] = d[n - 1]; }
        else { uint8_t prev = 0; for (size_t i = 0; i < n; i++) { uint8_t z = d[i]; int8_t df = (int8_t)((z >> 1) ^ (uint8_t)(-(int)(z & 1))); prev = (uint8_t)(prev + df); out[i] = prev; } }
        return p - p0; }
    if (id == 13 || id == 14) { size_t ns = n / 3 * 2; int mode = get16(p); p += 2; uint32_t ne = get32(p); p += 4; const uint8_t *ex = p; p += 2 * ne;
        B by(ns); for (size_t q = 0; q < ns; q += blk) p += dec_bytes(p, by.data() + q, min(blk, ns - q));
        uint32_t k = 0; for (size_t i = 0, j = 0; j < ns; i += 3, j += 2) {
            uint32_t a = by[j] == 255 ? get16(ex + 2 * k++) : by[j] + mode - 128, b = by[j + 1] == 255 ? get16(ex + 2 * k++) : by[j + 1] + mode - 128;
            out[i] = a & 255; out[i + 1] = (a >> 8) | ((b & 15) << 4); out[i + 2] = b >> 4; }
        size_t t = n - ns / 2 * 3; memcpy(out + ns / 2 * 3, p, t); p += t; return p - p0; }
    if (id == 65 || id == 66) { size_t n0 = get32(p); p += 4; B l0(n0), l1(n - n0);
        for (B *l : {&l0, &l1}) for (size_t q = 0; q < l->size(); q += blk) p += dec_bytes(p, l->data() + q, min(blk, l->size() - q));
        size_t a = 0, b = 0; int prev = 0; for (size_t i = 0; i < n; i++) { int v = prev & 1 ? l1[b++] : l0[a++]; out[i] = (uint8_t)v; prev = v; }
        return p - p0; }
    if (id == 64) { size_t n0 = get32(p); p += 4; vector<uint16_t> s0(n0), s1(n - n0); p += rans_dec(p, s0.data(), n0, 256); p += rans_dec(p, s1.data(), n - n0, 256);
        size_t a = 0, b = 0; int prev = 0; for (size_t i = 0; i < n; i++) { int v = prev & 1 ? s1[b++] : s0[a++]; out[i] = (uint8_t)v; prev = v; }
        return p - p0; }
    if (id == 12) { size_t ns = n / 3 * 2; vector<uint16_t> s(ns); p += rans_dec(p, s.data(), ns, 4096);
        for (size_t i = 0, j = 0; j < ns; i += 3, j += 2) { out[i] = s[j] & 255; out[i + 1] = (s[j] >> 8) | ((s[j + 1] & 15) << 4); out[i + 2] = s[j + 1] >> 4; }
        size_t t = n - ns / 2 * 3; memcpy(out + ns / 2 * 3, p, t); p += t; return p - p0; }
    int L = (id & 31) == 0 ? 1 : (id & 31); B lane;
    for (int l = 0; l < L; l++) { size_t ln = (n - l + L - 1) / L; lane.resize(ln);
        size_t bl = (id & 128) ? ln : blk;
        for (size_t q = 0; q < ln; q += bl) p += dec_bytes(p, lane.data() + q, min(bl, ln - q));
        if (L == 1) memcpy(out, lane.data(), ln); else for (size_t i = l, k = 0; i < n; i += L, k++) out[i] = lane[k]; }
    return p - p0;
}

// ---------------- Sets labelling (same 7 features and k-means as hs_zstd / hs_wrap), up to 4 classes, no run smoothing
// (for entropy coding a class switch costs nothing but its map entry, so single windows are allowed to change class)
static size_t SW = 65536;   // Sets window: the standard 64 KB, or 65532 (a multiple of 12, so 2/3/4-byte and 12-bit units stay aligned when classes are joined); hxs tries both
static vector<int> sets_labels(const uint8_t *in, size_t n) {
    size_t nw = (n + SW - 1) / SW; vector<array<double, 7>> F(nw);
    for (size_t w = 0; w < nw; w++) { size_t L = min(SW, n - w * SW); double h[256] = {0}; for (size_t i = 0; i < L; i++) h[in[w * SW + i]]++;
        double e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double q = h[i] / L; e -= q * log2(q); }
        double al = 0, dg = 0, hi = 0, ct = 0; for (int i = 65; i < 91; i++) al += h[i]; for (int i = 97; i < 123; i++) al += h[i];
        for (int i = 48; i < 58; i++) dg += h[i]; for (int i = 128; i < 256; i++) hi += h[i]; for (int i = 0; i < 32; i++) ct += h[i]; ct += h[127] - h[10] - h[13] - h[9];
        double sp = h[32] + h[10] + h[13] + h[9]; F[w] = {ct / L, al / L, dg / L, sp / L, hi / L, h[0] / L, e / 8}; }
    int K = nw >= 16 ? 4 : nw >= 4 ? 2 : 1; vector<int> a(nw, 0); if (K == 1) return a;
    auto d2 = [](const array<double, 7> &x, const array<double, 7> &y) { double s = 0; for (int k = 0; k < 7; k++) s += (x[k] - y[k]) * (x[k] - y[k]); return s; };
    vector<array<double, 7>> C(1, F[0]);
    for (int k = 1; k < K; k++) { size_t b = 0; double bd = -1; for (size_t j = 0; j < nw; j++) { double d = 1e300; for (auto &c : C) d = min(d, d2(F[j], c)); if (d > bd) { bd = d; b = j; } } C.push_back(F[b]); }
    for (int it = 0; it < 20; it++) {
        for (size_t j = 0; j < nw; j++) { int b = 0; double bd = d2(F[j], C[0]); for (int k = 1; k < K; k++) { double d = d2(F[j], C[k]); if (d < bd) { bd = d; b = k; } } a[j] = b; }
        for (int k = 0; k < K; k++) { array<double, 7> sm{}; size_t c = 0; for (size_t j = 0; j < nw; j++) if (a[j] == k) { for (int q = 0; q < 7; q++) sm[q] += F[j][q]; c++; }
            if (c) for (int q = 0; q < 7; q++) C[k][q] = sm[q] / c; } }
    vector<size_t> cnt(K, 0); for (int x : a) cnt[x]++; int big = (int)(max_element(cnt.begin(), cnt.end()) - cnt.begin());
    for (auto &x : a) if (cnt[x] < 4) x = big;   // a class needs at least 4 windows (256 KB)
    return a;
}

#ifndef HX_NO_MAIN
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: entbench huf|fse|rans|ac0|hx|hxs FILE [BLOCK_KB] [THREADS]\n"); return 2; }
    string m = argv[1]; FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET); B in(n); if (fread(in.data(), 1, n, f) != n) return 1; fclose(f);
    // hxd (OURS): cascade line 2 in front - content-defined chunks (gear hash, about 2 KB, at least 512 bytes), every exact repeat of an earlier
    // chunk becomes a reference; only the new chunks go on to Sets + Exchange (hxs). Kept only when it makes the file smaller.
    bool HXD = m == "hxd"; B orig; vector<uint32_t> clen; vector<int32_t> cref; size_t ddbytes = 0; bool dd = false; double ddsec = 0;
    if (HXD) { double td = now(); m = "hxs"; uint64_t g[256], sd = 20261103; for (auto &x : g) { uint64_t z = (sd += 0x9e3779b97f4a7c15ULL); z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL; z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL; x = z ^ (z >> 31); }
        vector<size_t> cst; size_t st = 0; uint64_t hh = 0;
        for (size_t i = 0; i < n; i++) { hh = (hh << 1) + g[in[i]]; size_t L = i + 1 - st; if ((L >= 512 && (hh >> 53) == 0) || L >= 65536) { cst.push_back(st); clen.push_back((uint32_t)L); st = i + 1; hh = 0; } }
        if (st < n) { cst.push_back(st); clen.push_back((uint32_t)(n - st)); }
        unordered_map<uint64_t, int32_t> seen; B U; U.reserve(n);
        for (size_t c = 0; c < cst.size(); c++) { uint64_t fh = 1469598103934665603ULL ^ clen[c]; const uint8_t *q = in.data() + cst[c];
            for (size_t i = 0; i < clen[c]; i += 8) { uint64_t v = 0; memcpy(&v, q + i, min<size_t>(8, clen[c] - i)); fh = (fh ^ v) * 1099511628211ULL; fh ^= fh >> 29; }
            auto it = seen.find(fh); int32_t r = -1;
            if (it != seen.end() && clen[it->second] == clen[c] && memcmp(in.data() + cst[it->second], q, clen[c]) == 0) r = it->second;
            else { seen.emplace(fh, (int32_t)c); U.insert(U.end(), q, q + clen[c]); }
            cref.push_back(r); }
        auto vb = [](uint64_t v) { size_t k = 1; while (v >= 128) { v >>= 7; k++; } return k; };
        ddbytes = vb(cst.size()); for (size_t c = 0; c < cst.size(); c++) ddbytes += vb(clen[c]) + vb((uint64_t)(cref[c] + 1));
        if (U.size() + ddbytes < n) { dd = true; orig.swap(in); in.swap(U); n = in.size(); }
        ddsec = now() - td; }
    size_t blk = (argc > 3 ? atol(argv[3]) : 128) * 1024; int T = argc > 4 ? atoi(argv[4]) : 1;
    if (m == "huf" && blk > HUF_BLOCKSIZE_MAX) blk = HUF_BLOCKSIZE_MAX;
    bool HX = m == "hx" || m == "hxs";
    if (HX || HXD) { g_w16 = detect_width(in.data(), n, 2); g_w8 = detect_width(in.data(), n, 1); g_rec = detect_record(in.data(), n); fprintf(stderr, "widths: 16-bit %zu, 8-bit %zu, record %zu\n", g_w16, g_w8, g_rec); }
    size_t seg = HX ? (1u << 20) - (1u << 20) % 12 : blk;   // unit of work: one block (hx: a 1 MB segment cut to a multiple of 12, so 2/3/4-byte and 12-bit units never straddle)
    if (m == "ac0") { seg = n; T = 1; }
    double t0 = now();
    // hxs: sort the 64 KB windows into Sets classes, lay the classes one after another (each in file order), code each class
    vector<int> lab; vector<size_t> worder; B P; const uint8_t *src = in.data(); vector<size_t> soff, slen; size_t mapbytes = 0; vector<B> part; size_t nseg = 0;
    auto build = [&](bool useSets) {
        soff.clear(); slen.clear(); worder.clear(); mapbytes = 0; src = in.data();
        if (useSets) { lab = sets_labels(in.data(), n); size_t nw = lab.size(); int K = *max_element(lab.begin(), lab.end()) + 1;
            P.resize(n); size_t pos = 0;
            for (int k = 0; k < K; k++) { size_t cs0 = pos;
                for (size_t w = 0; w < nw; w++) if (lab[w] == k) { size_t L = min(SW, n - w * SW); memcpy(P.data() + pos, in.data() + w * SW, L); worder.push_back(w); pos += L; }
                for (size_t q = cs0; q < pos; q += seg) { soff.push_back(q); slen.push_back(min(seg, pos - q)); } }
            src = P.data(); mapbytes = (nw * 2 + 7) / 8;   // class map: 2 bits per window
        } else for (size_t q = 0; q < n; q += seg) { soff.push_back(q); slen.push_back(min(seg, n - q)); }
        nseg = soff.size(); part.assign(nseg, B());
        auto encode = [&](size_t a, size_t b) { for (size_t k = a; k < b; k++) { size_t off = soff[k], len = slen[k];
            if (m == "ac0") ac0_enc(in.data(), n, part[k]); else if (HX) hx_seg_enc(src + off, len, min(blk, (size_t)HUF_BLOCKSIZE_MAX), part[k]); else enc_bytes(m, in.data() + off, len, part[k]); } };
        vector<thread> th; for (int t = 0; t < T; t++) th.emplace_back(encode, nseg * t / T, nseg * (t + 1) / T); for (auto &x : th) x.join();
        size_t tot = mapbytes; for (auto &q : part) tot += q.size() + 4; return tot; };
    bool sets = false;
    if (m == "hxs" && dd) { /* compared below against plain hxs on the whole file */ }
    if (m == "hxs") { SW = 65536; size_t s64 = build(true); SW = 65532; size_t s12 = build(true); size_t plain = build(false);   // Sets kept only when it makes the file smaller (1 flag byte)
        if (min(s64, s12) < plain) { SW = s64 <= s12 ? 65536 : 65532; build(true); sets = true; } }
    else build(false);
    if (m == "hxs" && !sets) worder.clear();
    double cs = now() - t0 + ddsec;
    B all; put32(all, (uint32_t)nseg); vector<size_t> offs(nseg); all.resize(all.size() + mapbytes + (m == "hxs") + (HXD ? 1 + (dd ? ddbytes : 0) : 0), 0);   // flags, class map and chunk table counted in the size
    for (size_t k = 0; k < nseg; k++) put32(all, (uint32_t)part[k].size());     // segment table, counted in the size
    size_t base = all.size(); for (size_t k = 0; k < nseg; k++) { offs[k] = all.size() - base; all.insert(all.end(), part[k].begin(), part[k].end()); }
    const uint8_t *body = all.data() + base;
    B out(n, 0), Q(sets ? n : 0), F(dd ? orig.size() : 0); double best = 1e9;
    for (int rep = 0; rep < 5 && !(rep >= 2 && best > 2.0); rep++) {   // best of 5 (best of 2 when one decode takes over 2 s)
        double t1 = now(); uint8_t *dst = sets ? Q.data() : out.data();
        auto decode = [&](size_t a, size_t b) { for (size_t k = a; k < b; k++) { size_t off = soff[k], len = slen[k];
            if (m == "ac0") ac0_dec(body, all.size() - base, out.data(), n);
            else if (HX) hx_seg_dec(body + offs[k], dst + off, len, min(blk, (size_t)HUF_BLOCKSIZE_MAX));
            else dec_bytes(body + offs[k], out.data() + off, len); } };
        auto unsort = [&](size_t a, size_t b) { size_t pos = 0; for (size_t i = 0; i < a; i++) pos += min(SW, n - worder[i] * SW);   // put windows back in file order
            for (size_t i = a; i < b; i++) { size_t w = worder[i], L = min(SW, n - w * SW); memcpy(out.data() + w * SW, Q.data() + pos, L); pos += L; } };
        if (T == 1) { decode(0, nseg); if (sets) unsort(0, worder.size()); }
        else { { vector<thread> th; for (int t = 0; t < T; t++) th.emplace_back(decode, nseg * t / T, nseg * (t + 1) / T); for (auto &x : th) x.join(); }
               if (sets) { vector<thread> th; size_t nw = worder.size(); for (int t = 0; t < T; t++) th.emplace_back(unsort, nw * t / T, nw * (t + 1) / T); for (auto &x : th) x.join(); } }
        if (dd) { size_t up = 0, op = 0; vector<size_t> cs0(clen.size());   // put the repeats back (one pass of copies)
            for (size_t c = 0; c < clen.size(); c++) { cs0[c] = op; if (cref[c] < 0) { memcpy(F.data() + op, out.data() + up, clen[c]); up += clen[c]; } else memcpy(F.data() + op, F.data() + cs0[cref[c]], clen[c]); op += clen[c]; } }
        best = min(best, now() - t1);
    }
    bool ok = dd ? memcmp(orig.data(), F.data(), orig.size()) == 0 : memcmp(in.data(), out.data(), n) == 0;
    if (dd) n = orig.size();
    printf("dedupe %d sets %d mode %s block_kb %zu threads %d in %zu out %zu per_million %.0f comp_s %.4f dec_s %.5f dec_MBps %.0f exact %s\n",
           (int)dd, (int)sets, HXD ? "hxd" : m.c_str(), blk / 1024, T, n, all.size(), 1e6 * all.size() / n, cs, best, n / best / 1e6, ok ? "yes" : "NO");
    return ok ? 0 : 3;
}
#endif
