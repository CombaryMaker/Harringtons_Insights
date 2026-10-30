// HCM engine test bench: configurable variants for speed experiments.
//   BUCKETS=0 INTMIX=0  -> must reproduce HCM v2 output exactly
//   BUCKETS=1           -> cache-line nibble buckets (new format)
//   INTMIX=1            -> integer mixer, squash and refinement (new format)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "../tables.inc"
#include "../part_math.inc"

#ifndef BUCKETS
#define BUCKETS 0
#endif
#ifndef INTMIX
#define INTMIX 0
#endif
#ifndef BBITS
#define BBITS 18
#endif
#ifndef PREFETCH
#define PREFETCH 1
#endif

static double ST[4096];
static int RT[256];
static int16_t STI[4096];   // stretch(p) * 256, clamped to +-2047
static int16_t SQI[4096];   // squash(x / 256) for x = -2048..2047

static double squash_d(double x) {
    if (x > 20) x = 20;
    else if (x < -20) x = -20;
    return 4096.0 / (1.0 + hcm_exp(-x));
}

static void init_tables(void) {
    for (int p = 1; p < 4096; p++) ST[p] = hcm_log((double)p / (double)(4096 - p));
    ST[0] = ST[1];
    for (int n = 0; n < 256; n++) RT[n] = (int)(65536.0 / ((double)n + 1.6));
    for (int p = 0; p < 4096; p++) {
        double v = ST[p] * 256.0;
        int s = (int)(v < 0 ? v - 0.5 : v + 0.5);
        if (s > 2047) s = 2047;
        if (s < -2047) s = -2047;
        STI[p] = (int16_t)s;
    }
    for (int x = -2048; x < 2048; x++) {
        int p = (int)squash_d(x / 256.0);
        if (p < 1) p = 1;
        if (p > 4095) p = 4095;
        SQI[x + 2048] = (int16_t)p;
    }
}

// ---------------------------------------------------------------- bit I/O
struct BitWriter { uint8_t *data; size_t size, cap; uint32_t cur; int nbits; };
static void bw_init(BitWriter *w, size_t guess) {
    w->cap = guess < 64 ? 64 : guess; w->data = (uint8_t *)malloc(w->cap);
    w->size = 0; w->cur = 0; w->nbits = 0;
}
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
static inline int br_bit(BitReader *r) {
    int b = 0;
    if (r->pos < r->nbits) b = (r->data[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos++;
    return b;
}
static uint32_t br_get(BitReader *r, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)br_bit(r); return v; }

// ---------------------------------------------------------------- coder
static const uint64_t TOP = 0xFFFFFFFFULL, HALF = 1ULL << 31, Q1 = 1ULL << 30, Q3 = 3ULL << 30;
struct Encoder { uint64_t lo, hi, pend; BitWriter *w; };
static inline void enc_out(Encoder *e, int bit) { bw_bit(e->w, bit); for (; e->pend; e->pend--) bw_bit(e->w, 1 - bit); }
// y = 1 takes [0, p), y = 0 takes [p, 4096)
static inline void enc_bit(Encoder *e, int y, uint64_t p) {
    uint64_t rng = e->hi - e->lo + 1;
    if (y) e->hi = e->lo + ((rng * p) >> 12) - 1;
    else e->lo = e->lo + ((rng * p) >> 12);
    for (;;) {
        if (e->hi < HALF) enc_out(e, 0);
        else if (e->lo >= HALF) { enc_out(e, 1); e->lo -= HALF; e->hi -= HALF; }
        else if (e->lo >= Q1 && e->hi < Q3) { e->pend++; e->lo -= Q1; e->hi -= Q1; }
        else break;
        e->lo <<= 1; e->hi = (e->hi << 1) | 1;
    }
}
static void enc_finish(Encoder *e) { e->pend++; enc_out(e, e->lo < Q1 ? 0 : 1); }
struct Decoder { uint64_t lo, hi, v; BitReader *r; };
static inline int dec_bit(Decoder *d, uint64_t p) {
    uint64_t rng = d->hi - d->lo + 1;
    int y = ((d->v - d->lo + 1) * 4096 - 1) < p * rng;   // same as target < p
    if (y) d->hi = d->lo + ((rng * p) >> 12) - 1;
    else d->lo = d->lo + ((rng * p) >> 12);
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
enum { NORD = 6, NM = NORD + 3, NIN = NM + 2, TBITS = 22, APM_ROWS = 65536, MTAB_BITS = 20 };
static const int ORDERS[NORD] = {1, 2, 3, 4, 6, 8};
static const uint32_t MASK = (1u << TBITS) - 1;
static const double APM_W = 0.9;
static const double LR = 0.008;

// entry = (p ^ 0x8000) | (count << 16), so an all-zero table means p = 32768, count 0
static inline int ent_p(uint32_t e) { return (int)((e & 0xFFFF) ^ 0x8000); }
static inline int ent_n(uint32_t e) { return (int)(e >> 16); }
static inline uint32_t ent_make(int p, int n) { return ((uint32_t)p ^ 0x8000u) | ((uint32_t)n << 16); }

struct Model {
    uint32_t *T[NM];
    void *Traw[NM];
    int limits[NM];
#if INTMIX
    int32_t (*w)[NIN];
    int x[NIN];
    int pm, sdot, aj, awt;
#else
    double (*w)[NIN];
    double st[NIN];
    double pm, af;
    int aj;
#endif
    uint16_t *apm;
    uint8_t *apm_ok;
    uint16_t row0[33];
    uint8_t *buf; size_t n;
    uint32_t c0; int bitpos; uint32_t nib;
    uint32_t h[NM];
    uint32_t word, pword;
    int32_t *mtab; int64_t mptr, mlen;
    uint16_t mm[64];
    int last;
    uint32_t *slot[NM];
    int mexp, mk;
    void *wsel;
    uint16_t *arow;
};

static void *alloc64(size_t bytes, void **raw) {
    *raw = calloc(bytes + 64, 1);
    if (!*raw) return NULL;
    return (void *)(((uintptr_t)*raw + 63) & ~(uintptr_t)63);
}

static void ctx_byte(Model *m) {
    const uint8_t *b = m->buf; size_t n = m->n;
    for (int k = 0; k < NORD; k++) {
        int o = ORDERS[k];
        uint32_t x = (uint32_t)(o + 1) * 0x9E3779B1u;
        for (int j = 1; j <= o; j++) { uint32_t c = n >= (size_t)j ? b[n - j] : 0; x = (x ^ c) * 0x01000193u + (uint32_t)j; }
        m->h[k] = x * 0x2545F491u;
    }
    m->h[NORD] = 0;
    m->h[NORD + 1] = m->word * 0x6F4F2A35u + 0x51ED27u;
    m->h[NORD + 2] = (m->pword * 0x3B9AC9FBu) ^ (m->word * 0x6F4F2A35u) ^ 0x7A3D1u;
    m->last = n ? b[n - 1] : 0;
}

#if BUCKETS
static inline uint32_t bucket_hash(uint32_t h, uint32_t c0) {
    uint32_t x = h + c0 * 0x9E3779B1u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x >> (32 - BBITS);
}
static inline void set_buckets(Model *m) {
    for (int i = 0; i < NM; i++) {
        if (i == NORD) continue;
        uint32_t *base = m->T[i] + ((size_t)bucket_hash(m->h[i], m->c0) << 4);
        m->slot[i] = base;  // slot index added in predict
#if PREFETCH
        __builtin_prefetch(base);
#endif
    }
}
#endif

static int model_init(Model *m, size_t capacity) {
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < NM; i++) {
#if BUCKETS
        size_t entries = (i == NORD) ? 256 : ((size_t)16 << BBITS);
#else
        size_t entries = (size_t)1 << TBITS;
#endif
        m->T[i] = (uint32_t *)alloc64(entries * 4, &m->Traw[i]);
        if (!m->T[i]) return -1;
        m->limits[i] = i == 0 ? 60 : 255;
    }
#if INTMIX
    m->w = (int32_t (*)[NIN])malloc(sizeof(int32_t) * NIN * 1024);
    for (int r = 0; r < 1024; r++) for (int i = 0; i < NIN; i++) m->w[r][i] = 19661;  // 0.3
    for (int j = 0; j < 33; j++) { int x = j * 128 - 2048; if (x > 2047) x = 2047; m->row0[j] = (uint16_t)(SQI[x + 2048] * 16); }
#else
    m->w = (double (*)[NIN])malloc(sizeof(double) * NIN * 1024);
    for (int r = 0; r < 1024; r++) for (int i = 0; i < NIN; i++) m->w[r][i] = 0.3;
    for (int j = 0; j < 33; j++) m->row0[j] = (uint16_t)(int32_t)(squash_d((j - 16) / 2.0) * 16);
#endif
    m->apm = (uint16_t *)malloc(sizeof(uint16_t) * 33 * APM_ROWS);
    m->apm_ok = (uint8_t *)calloc(APM_ROWS, 1);
    m->buf = (uint8_t *)malloc(capacity ? capacity : 1);
    m->mtab = (int32_t *)calloc((size_t)1 << MTAB_BITS, 4);
    if (!m->w || !m->apm || !m->apm_ok || !m->buf || !m->mtab) return -1;
    m->c0 = 1; m->nib = 1;
    for (int i = 0; i < 64; i++) m->mm[i] = 49152;
    ctx_byte(m);
#if BUCKETS
    set_buckets(m);
#endif
    return 0;
}

static void model_free(Model *m) {
    for (int i = 0; i < NM; i++) free(m->Traw[i]);
    free(m->w); free(m->apm); free(m->apm_ok); free(m->buf); free(m->mtab);
}

static inline int predict(Model *m) {
    uint32_t c0 = m->c0;
#if BUCKETS
    for (int i = 0; i < NM; i++) if (i != NORD) m->slot[i] = (uint32_t *)((uintptr_t)m->slot[i] & ~(uintptr_t)63) + m->nib;
    m->slot[NORD] = m->T[NORD] + c0;
#else
    uint32_t cm = c0 * 0x9E3779B1u;
    for (int i = 0; i < NM; i++) m->slot[i] = m->T[i] + ((m->h[i] ^ cm) & MASK);
    m->slot[NORD] = m->T[NORD] + c0;
#endif
    // long-match input
    m->mexp = -1;
    if (m->mlen > 0 && (size_t)m->mptr < m->n) {
        int pb = m->buf[m->mptr];
        if ((uint32_t)((pb | 256) >> (8 - m->bitpos)) == c0) m->mexp = (pb >> (7 - m->bitpos)) & 1;
    }
    int ms = m->mlen == 0 ? 0 : m->mlen < 16 ? 1 : m->mlen < 32 ? 2 : 3;
    int arow_i = (int)(c0 + 256 * (uint32_t)m->last);
    if (!m->apm_ok[arow_i]) { memcpy(m->apm + (size_t)arow_i * 33, m->row0, sizeof(m->row0)); m->apm_ok[arow_i] = 1; }
    m->arow = m->apm + (size_t)arow_i * 33;
#if INTMIX
    for (int i = 0; i < NM; i++) m->x[i] = STI[ent_p(*m->slot[i]) >> 4];
    if (m->mexp >= 0) { m->mk = m->mlen < 63 ? (int)m->mlen : 63; int s = STI[m->mm[m->mk] >> 4]; m->x[NM] = m->mexp ? s : -s; }
    else m->x[NM] = 0;
    m->x[NM + 1] = 64;
    int32_t *ws = m->w[c0 + 256 * ms];
    m->wsel = ws;
    int64_t dot = 0;
    for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * m->x[i];
    int s = (int)(dot >> 16);
    if (s > 2047) s = 2047;
    if (s < -2047) s = -2047;
    m->sdot = s;
    m->pm = SQI[s + 2048];
    int pos = s + 2048;
    int j = pos >> 7, wt = pos & 127;
    m->aj = j; m->awt = wt;
    int pa = (m->arow[j] * (128 - wt) + m->arow[j + 1] * wt) >> 11;
    int p = (m->pm + 9 * pa) / 10;
#else
    for (int i = 0; i < NM; i++) m->st[i] = ST[ent_p(*m->slot[i]) >> 4];
    if (m->mexp >= 0) { m->mk = m->mlen < 63 ? (int)m->mlen : 63; int pc = m->mm[m->mk] >> 4; m->st[NM] = m->mexp ? ST[pc] : -ST[pc]; }
    else m->st[NM] = 0.0;
    m->st[NM + 1] = 0.25;
    double *ws = m->w[c0 + 256 * ms];
    m->wsel = ws;
    double dot = 0.0;
    for (int i = 0; i < NIN; i++) dot += ws[i] * m->st[i];
    m->pm = squash_d(dot);
    double sd = dot < 7.999 ? dot : 7.999;
    sd = sd > -7.999 ? sd : -7.999;
    double pos = (sd + 8.0) * 2.0;
    int j = (int)pos;
    double f = pos - j;
    m->aj = j; m->af = f;
    double pa = (m->arow[j] * (1 - f) + m->arow[j + 1] * f) / 16.0;
    int p = (int)(m->pm * (1 - APM_W) + pa * APM_W);
#endif
    return p < 1 ? 1 : (p > 4095 ? 4095 : p);
}

static inline void update(Model *m, int y) {
#if INTMIX
    int err = (y << 12) - m->pm;
    int32_t *ws = (int32_t *)m->wsel;
    for (int i = 0; i < NIN; i++) ws[i] += (m->x[i] * err + 1024) >> 11;
#else
    double err = (y - m->pm / 4096.0) * LR;
    double *ws = (double *)m->wsel;
    for (int i = 0; i < NIN; i++) ws[i] += err * m->st[i];
#endif
    int tgt = y ? 65535 : 0;
    for (int i = 0; i < NM; i++) {
        uint32_t e = *m->slot[i];
        int p = ent_p(e), n = ent_n(e);
        p += (int)(((int64_t)(tgt - p) * RT[n]) >> 16);
        if (n < m->limits[i]) n++;
        *m->slot[i] = ent_make(p, n);
    }
    if (m->mexp >= 0) {
        int v = m->mm[m->mk];
        int hit = y == m->mexp ? 65535 : 0;
        m->mm[m->mk] = (uint16_t)(v + ((hit - v) >> 5));
    }
    uint16_t *row = m->arow;
    int j = m->aj, t16 = y ? 65535 : 0;
#if INTMIX
    int wt = m->awt;
    row[j] = (uint16_t)(row[j] + (((t16 - row[j]) * (128 - wt)) >> 13));
    row[j + 1] = (uint16_t)(row[j + 1] + (((t16 - row[j + 1]) * wt) >> 13));
#else
    row[j] = (uint16_t)(row[j] + ((int32_t)((int64_t)((t16 - row[j]) * (1 - m->af)) >> 6)));
    row[j + 1] = (uint16_t)(row[j + 1] + ((int32_t)((int64_t)((t16 - row[j + 1]) * m->af) >> 6)));
#endif
    m->c0 = (m->c0 << 1) | (uint32_t)y;
    m->nib = (m->nib << 1) | (uint32_t)y;
    m->bitpos++;
    if (m->bitpos == 4) {
        m->nib = 1;
#if BUCKETS
        set_buckets(m);
#endif
    } else if (m->bitpos == 8) {
        int byte = (int)(m->c0 & 255);
        m->buf[m->n++] = (uint8_t)byte;
        m->c0 = 1; m->bitpos = 0; m->nib = 1;
        if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122)) m->word = (m->word * 0x2C9277B5u) ^ (uint32_t)(byte | 32);
        else { if (m->word) m->pword = m->word; m->word = 0; }
        int64_t n = (int64_t)m->n; const uint8_t *b = m->buf;
        if (m->mlen > 0 && m->mptr < n - 1 && b[m->mptr] == byte) { m->mptr++; m->mlen++; }
        else m->mlen = 0;
        if (n >= 6) {
            uint32_t hh = 0;
            for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + b[n - j2]) & 0xFFFFF);
            if (m->mlen == 0) {
                int64_t cand = m->mtab[hh];
                if (cand > 0) {
                    int L = 0;
                    while (L < 32 && cand - 1 - L >= 0 && b[cand - 1 - L] == b[n - 1 - L]) L++;
                    if (L >= 6) { m->mptr = cand; m->mlen = L; }
                }
            }
            m->mtab[hh] = (int32_t)n;
        }
        ctx_byte(m);
#if BUCKETS
        set_buckets(m);
#endif
    }
}

static int compress(const uint8_t *data, size_t n, uint8_t **out, size_t *outn) {
    Model *m = (Model *)malloc(sizeof(Model));
    if (!m || model_init(m, n)) return -1;
    BitWriter w; bw_init(&w, n / 3 + 64); bw_put(&w, (uint32_t)n, 32);
    Encoder e = {0, TOP, 0, &w};
    for (size_t i = 0; i < n; i++) {
        int c = data[i];
        for (int k = 7; k >= 0; k--) { int y = (c >> k) & 1; int p = predict(m); enc_bit(&e, y, (uint64_t)p); update(m, y); }
    }
    enc_finish(&e); bw_flush(&w);
    model_free(m); free(m);
    *out = w.data; *outn = w.size;
    return 0;
}

static int decompress(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    BitReader r = {blob, (uint64_t)bn * 8, 0};
    uint32_t n = br_get(&r, 32);
    Decoder d = {0, TOP, 0, &r}; d.v = br_get(&r, 32);
    Model *m = (Model *)malloc(sizeof(Model));
    if (!m || model_init(m, n)) return -1;
    for (uint32_t i = 0; i < n; i++)
        for (int k = 0; k < 8; k++) { int p = predict(m); int y = dec_bit(&d, (uint64_t)p); update(m, y); }
    *out = m->buf; *outn = m->n; m->buf = NULL;
    model_free(m); free(m);
    return 0;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    // usage: engine REFDIR FILE...   (REFDIR "-" to skip the v2 comparison)
    init_tables();
    const char *ref = argv[1];
    double tc = 0, td = 0; uint64_t tin = 0, tout = 0; int allok = 1, allsame = 1;
    for (int a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *data = (uint8_t *)malloc(sz ? sz : 1); if (fread(data, 1, sz, f) != (size_t)sz) return 1; fclose(f);
        uint8_t *blob, *back; size_t bn, backn;
        double t0 = now(); compress(data, sz, &blob, &bn); double t1 = now(); decompress(blob, bn, &back, &backn); double t2 = now();
        int ok = backn == (size_t)sz && memcmp(back, data, sz) == 0;
        int same = -1;
        if (ref[0] != '-') {
            const char *base = strrchr(argv[a], '/'); base = base ? base + 1 : argv[a];
            char path[1024]; snprintf(path, sizeof path, "%s/%s.hcm", ref, base);
            FILE *g = fopen(path, "rb"); same = 0;
            if (g) { fseek(g, 0, SEEK_END); long rs = ftell(g); fseek(g, 0, SEEK_SET); uint8_t *rb = (uint8_t *)malloc(rs);
                     if (fread(rb, 1, rs, g) == (size_t)rs && (size_t)rs == bn && memcmp(rb, blob, bn) == 0) same = 1; fclose(g); free(rb); }
            if (!same) allsame = 0;
        }
        if (!ok) allok = 0;
        tc += t1 - t0; td += t2 - t1; tin += sz; tout += bn;
        printf("%-13s %8ld -> %7zu  per-million %7.0f  c %.3fs d %.3fs  ok %d same-as-v2 %d\n",
               strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a], sz, bn, bn * 1e6 / sz, t1 - t0, t2 - t1, ok, same);
        free(data); free(blob); free(back);
    }
    printf("TOTAL %llu -> %llu  per-million %.0f  compress %.2fs  decompress %.2fs  all-ok %d all-same %d\n",
           (unsigned long long)tin, (unsigned long long)tout, tout * 1e6 / tin, tc, td, allok, allsame);
    return 0;
}
