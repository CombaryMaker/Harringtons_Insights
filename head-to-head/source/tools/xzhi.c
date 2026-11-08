/* xz-hi (native) - smaller standard .xz files. Michael Hugh Harrington, MIT License, 2026.
 * C port of xz-hi/xz_hi.py: compressor-side search only (lc/lp/pb, match-finder depth and nice length,
 * optional delta/BCJ pre-filter, dictionary sized to the file). Output is a normal .xz that stock xz decodes.
 *   xz-hi IN OUT      (decompress with stock xz:  xz -dc IN > OUT)
 * Built with liblzma (XZ Utils, 0BSD). */
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *rd(const char *f, size_t *n) {
    FILE *p = fopen(f, "rb"); if (!p) { perror(f); exit(1); }
    fseek(p, 0, SEEK_END); long s = ftell(p); fseek(p, 0, SEEK_SET);
    uint8_t *b = malloc(s ? s : 1); if (fread(b, 1, s, p) != (size_t)s) { perror(f); exit(1); } fclose(p); *n = s; return b;
}
static int looks_ambiguous(const uint8_t *d, size_t n) { size_t k = 0; for (size_t i = 0; i < n; i++) k += d[i] >= 128; return n && k * 20 >= n; }
static int looks_binary(const uint8_t *d, size_t n) { size_t k = 0; for (size_t i = 0; i < n; i++) { uint8_t c = d[i]; k += c < 9 || (c > 13 && c < 32) || c == 127; } return n && k * 10 > n; }
static long record_length(const uint8_t *d, size_t n) {   /* HCM's record detector */
    static long lp[256][3]; for (int i = 0; i < 256; i++) lp[i][0] = lp[i][1] = lp[i][2] = -1;
    long cand = 0, cnt = 0, R = 0;
    for (size_t p = 0; p < n; p++) { long *l = lp[d[p]];
        long d1 = l[0] >= 0 ? (long)p - l[0] : 0, d2 = l[1] >= 0 ? l[0] - l[1] : -1, d3 = l[2] >= 0 ? l[1] - l[2] : -2;
        if (d1 > 1 && d1 == d2 && d2 == d3 && d1 < 65536) {
            if (d1 == cand) { if (++cnt > 64) cnt = 64; } else if (cnt > 0) cnt--; else { cand = d1; cnt = 1; }
            if (cnt >= 3 && cand != R) R = cand; }
        l[2] = l[1]; l[1] = l[0]; l[0] = (long)p; }
    return R;
}
/* compress with optional pre-filter; returns size or 0 on failure; output in *out (cap bytes) */
static size_t comp(const uint8_t *d, size_t n, uint8_t *out, size_t cap, int lc, int lp, int pb,
                   lzma_vli preid, uint32_t dist, uint32_t depth, uint32_t nice) {
    lzma_options_lzma o; if (lzma_lzma_preset(&o, 9 | LZMA_PRESET_EXTREME)) return 0;
    o.dict_size = n > 4096 ? (uint32_t)(n > 0x60000000 ? 0x60000000 : n) : 4096;
    o.lc = lc; o.lp = lp; o.pb = pb; o.nice_len = nice; o.depth = depth; o.mf = LZMA_MF_BT4;
    lzma_options_delta od = { .type = LZMA_DELTA_TYPE_BYTE, .dist = dist };
    lzma_filter f[3]; int k = 0;
    if (preid != LZMA_VLI_UNKNOWN) { f[k].id = preid; f[k].options = preid == LZMA_FILTER_DELTA ? &od : NULL; k++; }
    f[k].id = LZMA_FILTER_LZMA2; f[k].options = &o; k++; f[k].id = LZMA_VLI_UNKNOWN; f[k].options = NULL;
    size_t pos = 0;
    if (lzma_stream_buffer_encode(f, LZMA_CHECK_CRC32, NULL, d, n, out, &pos, cap) != LZMA_OK) return 0;
    return pos;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "xz-hi (native) - Michael Hugh Harrington, MIT License\n  xz-hi IN OUT   (output is a standard .xz; decode with stock xz -d)\n"); return 2; }
    size_t n; uint8_t *d = rd(argv[1], &n);
    size_t cap = lzma_stream_buffer_bound(n) + 65536;
    uint8_t *best = malloc(cap), *tmp = malloc(cap); size_t bestn = 0;
    if (lzma_easy_buffer_encode(9 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64, NULL, d, n, best, &bestn, cap) != LZMA_OK) { fprintf(stderr, "encode failed\n"); return 1; }
    size_t base = bestn;
    /* rank all (lc,lp,pb) with lc+lp<=4 */
    struct { size_t s; int lc, lp, pb; } c[45]; int nc = 0;
    for (int lc = 0; lc < 5; lc++) for (int lp = 0; lp < 3; lp++) for (int pb = 0; pb < 3; pb++) if (lc + lp <= 4) {
        size_t s = comp(d, n, tmp, cap, lc, lp, pb, LZMA_VLI_UNKNOWN, 0, 512, 273);
        c[nc].s = s ? s : (size_t)-1; c[nc].lc = lc; c[nc].lp = lp; c[nc].pb = pb; nc++; }
    for (int i = 0; i < nc; i++) for (int j = i + 1; j < nc; j++) if (c[j].s < c[i].s) { __typeof__(c[0]) t = c[i]; c[i] = c[j]; c[j] = t; }
    lzma_vli pid[32]; uint32_t pd[32]; int np = 0; pid[np] = LZMA_VLI_UNKNOWN; pd[np++] = 0;
    if (looks_binary(d, n) || looks_ambiguous(d, n)) {
        uint32_t ds[11] = {1, 2, 4, 8, 12, 13, 16, 26, 48, 216, 0}; int nd = 10; long r = record_length(d, n);
        int have = 0; for (int i = 0; i < nd; i++) have |= ds[i] == (uint32_t)r; if (r > 0 && r <= 256 && !have) ds[nd++] = (uint32_t)r;
        for (int i = 0; i < nd; i++) for (int j = i + 1; j < nd; j++) if (ds[j] < ds[i]) { uint32_t t = ds[i]; ds[i] = ds[j]; ds[j] = t; }
        for (int i = 0; i < nd; i++) { pid[np] = LZMA_FILTER_DELTA; pd[np++] = ds[i]; }
        lzma_vli bcj[6] = {LZMA_FILTER_X86, LZMA_FILTER_SPARC, LZMA_FILTER_POWERPC, LZMA_FILTER_ARM, LZMA_FILTER_ARMTHUMB, LZMA_FILTER_IA64};
        for (int i = 0; i < 6; i++) { pid[np] = bcj[i]; pd[np++] = 0; }
    }
    uint32_t depths[3] = {512, 0, 4000}, nices[2] = {273, 128};
    for (int ci = 0; ci < 4 && ci < nc; ci++) for (int p = 0; p < np; p++) for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) {
        size_t s = comp(d, n, tmp, cap, c[ci].lc, c[ci].lp, c[ci].pb, pid[p], pd[p], depths[a], nices[b]);
        if (s && s < bestn) { uint8_t *t = best; best = tmp; tmp = t; bestn = s; } }
    /* verify round trip with liblzma before writing */
    uint8_t *chk = malloc(n ? n : 1); size_t ip = 0, op = 0; uint64_t lim = UINT64_MAX;
    if (lzma_stream_buffer_decode(&lim, 0, NULL, best, &ip, bestn, chk, &op, n) != LZMA_OK || op != n || memcmp(chk, d, n)) { fprintf(stderr, "round-trip check failed\n"); return 1; }
    FILE *o = fopen(argv[2], "wb"); if (!o || fwrite(best, 1, bestn, o) != bestn) { perror(argv[2]); return 1; } fclose(o);
    printf("%s -> %s  %zu bytes (xz -9e: %zu bytes)\n", argv[1], argv[2], bestn, base);
    return 0;
}
