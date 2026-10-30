// HCM fast engine (v3 format experiments): nibble buckets + integer mixing,
// one fused loop per file with the hot state in local variables.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <emmintrin.h>
#include "../tables.inc"
#include "../part_math.inc"

#ifndef BBITS
#define BBITS 18
#endif
#ifndef LRSHIFT
#define LRSHIFT 11
#endif
#ifndef APMRATE
#define APMRATE 13
#endif
#ifndef CHECK2PF
#define CHECK2PF 0
#endif
#ifndef EXTRA
#define EXTRA 0      // 1 = add sparse/record contexts (compression experiments)
#endif

static double ST[4096];
static int RT[256];
static int RT15[256];
static int16_t STI[4096];
static int16_t SQI[4096];

static double squash_d(double x) {
    if (x > 20) x = 20; else if (x < -20) x = -20;
    return 4096.0 / (1.0 + hcm_exp(-x));
}
static void init_tables(void) {
    for (int p = 1; p < 4096; p++) ST[p] = hcm_log((double)p / (double)(4096 - p));
    ST[0] = ST[1];
    for (int n = 0; n < 256; n++) { RT[n] = (int)(65536.0 / ((double)n + 1.6)); RT15[n] = (int)(32768.0 / ((double)n + 1.6)); }
    for (int p = 0; p < 4096; p++) {
        double v = ST[p] * 256.0; int s = (int)(v < 0 ? v - 0.5 : v + 0.5);
        if (s > 2047) s = 2047; if (s < -2047) s = -2047; STI[p] = (int16_t)s;
    }
    for (int x = -2048; x < 2048; x++) {
        int p = (int)squash_d(x / 256.0); if (p < 1) p = 1; if (p > 4095) p = 4095; SQI[x + 2048] = (int16_t)p;
    }
}

// ---------------------------------------------------------------- bit I/O + coder
struct BitWriter { uint8_t *data; size_t size, cap; uint32_t cur; int nbits; };
static void bw_init(BitWriter *w, size_t guess) { w->cap = guess < 64 ? 64 : guess; w->data = (uint8_t *)malloc(w->cap); w->size = 0; w->cur = 0; w->nbits = 0; }
static inline void bw_bit(BitWriter *w, int bit) {
    w->cur = (w->cur << 1) | (uint32_t)bit;
    if (++w->nbits == 8) {
        if (w->size == w->cap) { w->cap *= 2; w->data = (uint8_t *)realloc(w->data, w->cap); }
        w->data[w->size++] = (uint8_t)w->cur; w->cur = 0; w->nbits = 0;
    }
}
static void bw_put(BitWriter *w, uint32_t v, int n) { for (int k = n - 1; k >= 0; k--) bw_bit(w, (v >> k) & 1); }
static void bw_flush(BitWriter *w) { while (w->nbits) bw_bit(w, 0); }
struct BitReader { const uint8_t *data; uint64_t nbits, pos; };
static inline int br_bit(BitReader *r) { int b = 0; if (r->pos < r->nbits) b = (r->data[r->pos >> 3] >> (7 - (r->pos & 7))) & 1; r->pos++; return b; }
static uint32_t br_get(BitReader *r, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)br_bit(r); return v; }

static const uint64_t TOP = 0xFFFFFFFFULL, HALF = 1ULL << 31, Q1 = 1ULL << 30, Q3 = 3ULL << 30;
struct Coder { uint64_t lo, hi, pend, v; BitWriter *w; BitReader *r; };
static inline void enc_out(Coder *e, int bit) { bw_bit(e->w, bit); for (; e->pend; e->pend--) bw_bit(e->w, 1 - bit); }
static inline void enc_bit(Coder *e, int y, uint64_t p) {
    uint64_t rng = e->hi - e->lo + 1;
    if (y) e->hi = e->lo + ((rng * p) >> 12) - 1; else e->lo = e->lo + ((rng * p) >> 12);
    for (;;) {
        if (e->hi < HALF) enc_out(e, 0);
        else if (e->lo >= HALF) { enc_out(e, 1); e->lo -= HALF; e->hi -= HALF; }
        else if (e->lo >= Q1 && e->hi < Q3) { e->pend++; e->lo -= Q1; e->hi -= Q1; }
        else break;
        e->lo <<= 1; e->hi = (e->hi << 1) | 1;
    }
}
static void enc_finish(Coder *e) { e->pend++; enc_out(e, e->lo < Q1 ? 0 : 1); }
static inline int dec_bit(Coder *d, uint64_t p) {
    uint64_t rng = d->hi - d->lo + 1;
    int y = ((d->v - d->lo + 1) * 4096 - 1) < p * rng;
    if (y) d->hi = d->lo + ((rng * p) >> 12) - 1; else d->lo = d->lo + ((rng * p) >> 12);
    for (;;) {
        if (d->hi < HALF) {}
        else if (d->lo >= HALF) { d->lo -= HALF; d->hi -= HALF; d->v -= HALF; }
        else if (d->lo >= Q1 && d->hi < Q3) { d->lo -= Q1; d->hi -= Q1; d->v -= Q1; }
        else break;
        d->lo <<= 1; d->hi = (d->hi << 1) | 1; d->v = (d->v << 1) | (uint64_t)br_bit(d->r);
    }
    return y;
}

// ---------------------------------------------------------------- model
#if EXTRA
enum { NORD = 6, NH = NORD + 2 + 3, NM = NH + 1 };   // + 3 extra hashed contexts
#else
enum { NORD = 6, NH = NORD + 2, NM = NH + 1 };      // hashed: orders, word, word pair; + order 0
#endif
enum { NIN = NM + 2, NINP = (NIN + 3) & ~3, APM_ROWS = 65536, MTAB_BITS = 20 };
static const int ORDERS[NORD] = {1, 2, 3, 4, 6, 8};

static inline int ent_p(uint32_t e) { return (int)(e & 0xFFFF); }
static inline uint32_t ent_make(int p, int n) { return (uint32_t)p | ((uint32_t)n << 16); }
static inline void bucket_reset(uint32_t *b, uint32_t chk) { __m128i v = _mm_set1_epi32(0x8000); for (int k = 0; k < 16; k += 4) _mm_store_si128((__m128i *)(b + k), v); b[0] = chk; }

#if CHECK2PF
#define PF2 __builtin_prefetch(bucket_addr(t->T[i], xs[i]) + 16);
#else
#define PF2
#endif
#ifndef CHECK
#define CHECK 2
#endif
static inline uint32_t bucket_mix(uint32_t h, uint32_t c0) {
    uint32_t x = h + c0 * 0x9E3779B1u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}
static inline uint32_t *bucket_addr(uint32_t *T, uint32_t x) {
#if CHECK == 2
    return T + ((size_t)((x >> (32 - BBITS)) & ~1u) << 4);
#else
    return T + ((size_t)(x >> (32 - BBITS)) << 4);
#endif
}
// slot 0 of each 16-entry bucket holds a check value for the context that owns it
static inline uint32_t *bucket_find(uint32_t *T, uint32_t x) {
    uint32_t *b = bucket_addr(T, x);
#if CHECK == 0
    return b;
#else
    uint32_t chk = (x & 0xFFFF) | 0x10000u;
#if CHECK == 1
    if (b[0] != chk) bucket_reset(b, chk);
    return b;
#else
    if (b[0] == chk) return b;
    uint32_t *b1 = b + 16;
    if (b1[0] == chk) return b1;
    uint32_t *v = (b[1] >> 16) <= (b1[1] >> 16) ? b : b1;   // replace the less-used one
    bucket_reset(v, chk);
    return v;
#endif
#endif
}

static void *alloc64(size_t bytes, void **raw) {
    *raw = calloc(bytes + 64, 1);
    return *raw ? (void *)(((uintptr_t)*raw + 63) & ~(uintptr_t)63) : NULL;
}

struct Tables {
    uint32_t *T[NH]; void *Traw[NH];
    alignas(64) uint32_t T0[256];
    alignas(16) int32_t w[1024][NINP];
    uint16_t *apm; uint8_t *apm_ok; uint16_t row0[33];
    int32_t *mtab;
};

static int tables_init(Tables *t) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < NH; i++) { t->T[i] = (uint32_t *)alloc64(((size_t)16 << BBITS) * 4, &t->Traw[i]); if (!t->T[i]) return -1; }
    for (int k = 0; k < 256; k++) t->T0[k] = 0x8000;
    for (int r = 0; r < 1024; r++) for (int i = 0; i < NINP; i++) t->w[r][i] = i < NIN ? 19661 : 0;
    for (int j = 0; j < 33; j++) { int x = j * 128 - 2048; if (x > 2047) x = 2047; t->row0[j] = (uint16_t)(SQI[x + 2048] * 16); }
    t->apm = (uint16_t *)malloc(sizeof(uint16_t) * 33 * APM_ROWS);
    t->apm_ok = (uint8_t *)calloc(APM_ROWS, 1);
    t->mtab = (int32_t *)calloc((size_t)1 << MTAB_BITS, 4);
    return (t->apm && t->apm_ok && t->mtab) ? 0 : -1;
}
static void tables_free(Tables *t) { for (int i = 0; i < NH; i++) free(t->Traw[i]); free(t->apm); free(t->apm_ok); free(t->mtab); }

// context hashes for the byte just completed (buf[0..n-1])
static inline void ctx_hashes(uint32_t *h, const uint8_t *b, size_t n, uint32_t word, uint32_t pword) {
    for (int k = 0; k < NORD; k++) {
        int o = ORDERS[k];
        uint32_t x = (uint32_t)(o + 1) * 0x9E3779B1u;
        for (int j = 1; j <= o; j++) { uint32_t c = n >= (size_t)j ? b[n - j] : 0; x = (x ^ c) * 0x01000193u + (uint32_t)j; }
        h[k] = x * 0x2545F491u;
    }
    h[NORD] = word * 0x6F4F2A35u + 0x51ED27u;
    h[NORD + 1] = (pword * 0x3B9AC9FBu) ^ (word * 0x6F4F2A35u) ^ 0x7A3D1u;
#if EXTRA
    uint32_t c1 = n >= 1 ? b[n - 1] : 0, c2 = n >= 2 ? b[n - 2] : 0, c3 = n >= 3 ? b[n - 3] : 0, c4 = n >= 4 ? b[n - 4] : 0;
    h[NORD + 2] = (c2 | (c3 << 8)) * 0x7FEB352Du + 0x1111;          // sparse: bytes 2 and 3 back
    h[NORD + 3] = (c2 | (c4 << 8)) * 0x846CA68Bu + 0x2222;          // sparse: bytes 2 and 4 back
    h[NORD + 4] = ((c1 & 0xE0) | ((c2 & 0xE0) << 8) | ((c3 & 0xE0) << 16)) * 0x9E3779B1u + 0x3333; // coarse order-3
#endif
}

template <int DEC>
static void run(Tables *t, uint8_t *buf, size_t n, Coder *cd) {
    uint32_t h[NH], *base[NH], *slot[NM];
    uint32_t word = 0, pword = 0;
    int64_t mptr = 0, mlen = 0;
    uint16_t mm[64];
    for (int i = 0; i < 64; i++) mm[i] = 49152;
    alignas(16) int32_t x[NINP];
    for (int i = 0; i < NINP; i++) x[i] = 0;
    static uint8_t NEXT[2][256];
    for (int n = 0; n < 256; n++) { NEXT[0][n] = (uint8_t)(n < 60 ? n + 1 : n); NEXT[1][n] = (uint8_t)(n < 255 ? n + 1 : n); }
    int last = 0;
    ctx_hashes(h, buf, 0, 0, 0);
    for (size_t pos = 0; pos < n; pos++) {
        int c = DEC ? 0 : buf[pos];
        // expected byte from the long match, if any
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? buf[mptr] : -1;
        int mk = mlen < 63 ? (int)mlen : 63;
        int ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        uint32_t c0 = 1;
        { uint32_t xs[NH];
          for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], 1); __builtin_prefetch(bucket_addr(t->T[i], xs[i])); PF2 }
          for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i]); }
        uint32_t nib = 1;
        for (int bit = 7; bit >= 0; bit--) {
            if (bit == 3) {
                nib = 1;
                uint32_t xs[NH];
                for (int i = 0; i < NH; i++) { xs[i] = bucket_mix(h[i], c0); __builtin_prefetch(bucket_addr(t->T[i], xs[i])); PF2 }
                for (int i = 0; i < NH; i++) base[i] = bucket_find(t->T[i], xs[i]);
            }
            for (int i = 0; i < NH; i++) slot[i] = base[i] + nib;
            slot[NH] = t->T0 + c0;
            for (int i = 0; i < NM; i++) x[i] = STI[ent_p(*slot[i]) >> 4];
            int mexp = -1;
            if (mbyte >= 0) {
                if ((uint32_t)((mbyte | 256) >> (bit + 1)) == c0) mexp = (mbyte >> bit) & 1;
                else mbyte = -1;
            }
            if (mexp >= 0) { int s = STI[mm[mk] >> 4]; x[NM] = mexp ? s : -s; } else x[NM] = 0;
            x[NM + 1] = 64;
            int32_t *ws = t->w[c0 + 256 * ms];
            int64_t dot = 0;
            for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
            int s = (int)(dot >> 16);
            s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
            int pm = SQI[s + 2048];
            int arow_i = (int)(c0 + 256 * (uint32_t)last);
            uint16_t *row = t->apm + (size_t)arow_i * 33;
            if (!t->apm_ok[arow_i]) { memcpy(row, t->row0, sizeof(t->row0)); t->apm_ok[arow_i] = 1; }
            if (bit > 0) __builtin_prefetch(t->apm + (size_t)(2 * c0 + 256 * (uint32_t)last) * 33);
            int apos = s + 2048, j = apos >> 7, wt = apos & 127;
            int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
            int p = (pm * 26 + pa * 230) >> 8;
            p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
            int y;
            if (DEC) y = dec_bit(cd, (uint64_t)p);
            else { y = (c >> bit) & 1; enc_bit(cd, y, (uint64_t)p); }
            // update
            int err = (y << 12) - pm;
            {
                __m128i ev = _mm_set1_epi32(err & 0xFFFF), rv = _mm_set1_epi32(1 << (LRSHIFT - 1));
                for (int i = 0; i < NINP; i += 4) {
                    __m128i xv = _mm_load_si128((const __m128i *)(x + i));
                    __m128i pv = _mm_srai_epi32(_mm_add_epi32(_mm_madd_epi16(xv, ev), rv), LRSHIFT);
                    __m128i wv = _mm_load_si128((const __m128i *)(ws + i));
                    _mm_store_si128((__m128i *)(ws + i), _mm_add_epi32(wv, pv));
                }
            }
            int tgt = y ? 65535 : 0;
            for (int i = 0; i < NM; i++) {
                uint32_t e = *slot[i];
                int pp = ent_p(e), nn = (int)(e >> 16);
                pp += ((tgt - pp) * RT15[nn]) >> 15;
                nn = NEXT[i != 0][nn];
                *slot[i] = ent_make(pp, nn);
            }
            if (mexp >= 0) { int v = mm[mk]; int hit = y == mexp ? 65535 : 0; mm[mk] = (uint16_t)(v + ((hit - v) >> 5)); }
            int t16 = y ? 65535 : 0;
            row[j] = (uint16_t)(row[j] + (((t16 - row[j]) * (128 - wt)) >> APMRATE));
            row[j + 1] = (uint16_t)(row[j + 1] + (((t16 - row[j + 1]) * wt) >> APMRATE));
            c0 = (c0 << 1) | (uint32_t)y;
            nib = (nib << 1) | (uint32_t)y;
        }
        int byte = (int)(c0 & 255);
        buf[pos] = (uint8_t)byte;
        size_t nb = pos + 1;
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) word = (word * 0x2C9277B5u) ^ (uint32_t)(byte | 32);
        else { if (word) pword = word; word = 0; }
        if (mlen > 0 && (size_t)mptr < nb - 1 && buf[mptr] == byte) { mptr++; mlen++; }
        else mlen = 0;
        if (nb >= 6) {
            uint32_t hh = 0;
            for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + buf[nb - j2]) & 0xFFFFF);
            if (mlen == 0) {
                int64_t cand = t->mtab[hh];
                if (cand > 0) {
                    int L = 0;
                    while (L < 32 && cand - 1 - L >= 0 && buf[cand - 1 - L] == buf[nb - 1 - L]) L++;
                    if (L >= 6) { mptr = cand; mlen = L; }
                }
            }
            t->mtab[hh] = (int32_t)nb;
        }
        last = byte;
        ctx_hashes(h, buf, nb, word, pword);
    }
}

static int compress(const uint8_t *data, size_t n, uint8_t **out, size_t *outn) {
    Tables *t = (Tables *)malloc(sizeof(Tables)); if (!t || tables_init(t)) return -1;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1); memcpy(buf, data, n);
    BitWriter w; bw_init(&w, n / 3 + 64); bw_put(&w, (uint32_t)n, 32);
    Coder e = {0, TOP, 0, 0, &w, NULL};
    run<0>(t, buf, n, &e);
    enc_finish(&e); bw_flush(&w);
    tables_free(t); free(t); free(buf);
    *out = w.data; *outn = w.size; return 0;
}
static int decompress(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    BitReader r = {blob, (uint64_t)bn * 8, 0};
    uint32_t n = br_get(&r, 32);
    Coder d = {0, TOP, 0, 0, NULL, &r}; d.v = br_get(&r, 32);
    Tables *t = (Tables *)malloc(sizeof(Tables)); if (!t || tables_init(t)) return -1;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    run<1>(t, buf, n, &d);
    tables_free(t); free(t);
    *out = buf; *outn = n; return 0;
}

static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }
int main(int argc, char **argv) {
    init_tables();
    double tc = 0, td = 0; uint64_t tin = 0, tout = 0; int allok = 1;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *data = (uint8_t *)malloc(sz ? sz : 1); if (fread(data, 1, sz, f) != (size_t)sz) return 1; fclose(f);
        uint8_t *blob, *back; size_t bn, backn;
        double t0 = now(); compress(data, sz, &blob, &bn); double t1 = now(); decompress(blob, bn, &back, &backn); double t2 = now();
        int ok = backn == (size_t)sz && memcmp(back, data, sz) == 0; if (!ok) allok = 0;
        tc += t1 - t0; td += t2 - t1; tin += sz; tout += bn;
        printf("%-13s %8ld -> %7zu  per-million %7.0f  c %.3fs d %.3fs  ok %d\n", strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a], sz, bn, bn * 1e6 / sz, t1 - t0, t2 - t1, ok);
        free(data); free(blob); free(back);
    }
    printf("TOTAL %llu -> %llu  per-million %.0f  compress %.2fs  decompress %.2fs  all-ok %d\n", (unsigned long long)tin, (unsigned long long)tout, tout * 1e6 / tin, tc, td, allok);
    return 0;
}
