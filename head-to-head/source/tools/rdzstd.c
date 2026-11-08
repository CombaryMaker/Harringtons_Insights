/* rd-zstd - record-length detection + delta in front of stock zstd. Michael Hugh Harrington, MIT License, 2026.
 * The record length is found with HCM's record detector; bytes are replaced by their difference from the byte one
 * record earlier, then zstd -19 compresses the result. If delta does not help, plain zstd -19 is kept (flag R = 0).
 *   rd-zstd c IN OUT      rd-zstd d IN OUT
 * Container: "RDZ1" | u32 record length R (0 = plain) | zstd frame. Built with libzstd (BSD). */
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *rd(const char *f, size_t *n) {
    FILE *p = fopen(f, "rb"); if (!p) { perror(f); exit(1); }
    fseek(p, 0, SEEK_END); long s = ftell(p); fseek(p, 0, SEEK_SET);
    uint8_t *b = malloc(s ? s : 1); if (fread(b, 1, s, p) != (size_t)s) { perror(f); exit(1); } fclose(p); *n = s; return b;
}
static void wr(const char *f, const void *a, size_t na, const void *b, size_t nb) {
    FILE *p = fopen(f, "wb"); if (!p || fwrite(a, 1, na, p) != na || (nb && fwrite(b, 1, nb, p) != nb)) { perror(f); exit(1); } fclose(p);
}
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
static size_t zc(const uint8_t *d, size_t n, uint8_t **out) {
    size_t cap = ZSTD_compressBound(n); *out = malloc(cap);
    size_t r = ZSTD_compress(*out, cap, d, n, 19); if (ZSTD_isError(r)) { fprintf(stderr, "zstd: %s\n", ZSTD_getErrorName(r)); exit(1); }
    return r;
}
int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "c") && strcmp(argv[1], "d"))) {
        fprintf(stderr, "rd-zstd - record-length delta + zstd -19 (Michael Hugh Harrington, MIT License)\n  rd-zstd c IN OUT\n  rd-zstd d IN OUT\n"); return 2; }
    size_t n; uint8_t *d = rd(argv[2], &n);
    if (argv[1][0] == 'c') {
        uint8_t *plain; size_t np = zc(d, n, &plain);
        long R = record_length(d, n); uint32_t used = 0; uint8_t *best = plain; size_t nb = np;
        if (R > 0 && (size_t)R < n) {
            uint8_t *t = malloc(n); memcpy(t, d, R); for (size_t i = R; i < n; i++) t[i] = (uint8_t)(d[i] - d[i - R]);
            uint8_t *dz; size_t nd = zc(t, n, &dz); free(t);
            if (nd < np) { best = dz; nb = nd; used = (uint32_t)R; }
        }
        uint8_t h[8] = {'R', 'D', 'Z', '1', used & 255, (used >> 8) & 255, (used >> 16) & 255, used >> 24};
        wr(argv[3], h, 8, best, nb);
        printf("%s -> %s  %zu bytes (record length %u%s)\n", argv[2], argv[3], nb + 8, used, used ? "" : ", plain zstd kept");
        return 0;
    }
    if (n < 8 || memcmp(d, "RDZ1", 4)) { fprintf(stderr, "not an rd-zstd file\n"); return 1; }
    size_t R = d[4] | d[5] << 8 | d[6] << 16 | (size_t)d[7] << 24;
    unsigned long long m = ZSTD_getFrameContentSize(d + 8, n - 8);
    if (m == ZSTD_CONTENTSIZE_ERROR || m == ZSTD_CONTENTSIZE_UNKNOWN) { fprintf(stderr, "bad zstd frame\n"); return 1; }
    uint8_t *o = malloc(m ? m : 1);
    size_t r = ZSTD_decompress(o, m, d + 8, n - 8); if (ZSTD_isError(r) || r != m) { fprintf(stderr, "zstd: decode failed\n"); return 1; }
    if (R) {
        size_t i = R;
        if (R >= 16) for (; i + 16 <= m; i += 16) for (int k = 0; k < 16; k++) o[i + k] = (uint8_t)(o[i + k] + o[i + k - R]);   /* no overlap: vectorises */
        for (; i < m; i++) o[i] = (uint8_t)(o[i] + o[i - R]);
    }
    wr(argv[3], o, m, NULL, 0);
    return 0;
}
