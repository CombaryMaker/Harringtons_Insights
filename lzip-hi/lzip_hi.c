/* lzip-hi - an encoder-side search for lzip (Michael Hugh Harrington, 8 Nov 2026). MIT License.
   Output is a standard single-member .lz file that stock lzip and lzlib decode; nothing new in the format.
   Built on lzlib 1.16 (Antonio Diaz Diaz, 2-clause BSD), unmodified.

   What it changes against "lzip -9" (dictionary 32 MiB, match length limit 273):
     1. dictionary size: the next power of two that covers the whole file (lzip allows up to 512 MiB), so files over
        32 MiB keep matches across the whole file; files under 32 MiB are unchanged on this point;
     2. match length limit: 9 values (16 .. 273) are tried on a sample of the file (up to 4 pieces of 2 MiB, Michael
        Harrington's sample-screening idea); the 2 best and the stock 273 are run on the whole file;
   and keeps the smallest result. Every candidate is decoded with lzlib and compared byte for byte before it is kept.

   usage: lzip-hi c IN OUT      compress (search)        lzip-hi s IN OUT   stock "lzip -9" settings (for comparison)
          lzip-hi d IN OUT      decompress (lzlib)       */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lzlib.h"
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

static uint8_t *read_file(const char *name, long *size) {
    FILE *f = fopen(name, "rb"); if (!f) { fprintf(stderr, "lzip-hi: cannot open %s\n", name); return 0; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = (uint8_t *)malloc(n + 1);
    if (!b || (n && fread(b, 1, n, f) != (size_t)n)) { fclose(f); free(b); return 0; } fclose(f); *size = n; return b;
}
static int write_file(const char *name, const uint8_t *b, long n) {
    FILE *f = fopen(name, "wb"); if (!f) return 0; int ok = n == 0 || fwrite(b, 1, n, f) == (size_t)n; return fclose(f) == 0 && ok;
}
static uint8_t *enc(const uint8_t *in, long insize, int dict, int mml, long *outlen) {
    if (dict < LZ_min_dictionary_size()) dict = LZ_min_dictionary_size(); if (dict > LZ_max_dictionary_size()) dict = LZ_max_dictionary_size();
    LZ_Encoder *e = LZ_compress_open(dict, mml, INT64_MAX); long delta = insize / 4 + 64, outsize = delta, inpos = 0, outpos = 0;
    uint8_t *out = (uint8_t *)malloc(outsize); bool err = false;
    if (!e || LZ_compress_errno(e) != LZ_ok || !out) { free(out); LZ_compress_close(e); return 0; }
    for (;;) {
        int r = LZ_compress_write(e, in + inpos, MIN(INT_MAX, insize - inpos)); if (r < 0) { err = true; break; } inpos += r;
        if (inpos >= insize) LZ_compress_finish(e);
        r = LZ_compress_read(e, out + outpos, MIN(INT_MAX, outsize - outpos)); if (r < 0) { err = true; break; } outpos += r;
        if (LZ_compress_finished(e) == 1) break;
        if (outpos >= outsize) { uint8_t *t; outsize += delta; t = (uint8_t *)realloc(out, outsize); if (!t) { err = true; break; } out = t; }
    }
    if (LZ_compress_close(e) < 0) err = true; if (err) { free(out); return 0; } *outlen = outpos; return out;
}
static uint8_t *dec(const uint8_t *in, long insize, long *outlen) {
    LZ_Decoder *d = LZ_decompress_open(); long delta = insize * 2 + 64, outsize = delta, inpos = 0, outpos = 0;
    uint8_t *out = (uint8_t *)malloc(outsize); bool err = false;
    if (!d || LZ_decompress_errno(d) != LZ_ok || !out) { free(out); LZ_decompress_close(d); return 0; }
    for (;;) {
        int r = LZ_decompress_write(d, in + inpos, MIN(INT_MAX, insize - inpos)); if (r < 0) { err = true; break; } inpos += r;
        if (inpos >= insize) LZ_decompress_finish(d);
        r = LZ_decompress_read(d, out + outpos, MIN(INT_MAX, outsize - outpos)); if (r < 0) { err = true; break; } outpos += r;
        if (LZ_decompress_finished(d) == 1) break;
        if (outpos >= outsize) { uint8_t *t; outsize += delta; t = (uint8_t *)realloc(out, outsize); if (!t) { err = true; break; } out = t; }
    }
    if (LZ_decompress_close(d) < 0) err = true; if (err) { free(out); return 0; } *outlen = outpos; return out;
}
static int stock_dict(long n) { int d = 1 << 25; return n < d ? (int)(n < 4096 ? 4096 : n) : d; }   /* lzip -9: 32 MiB, reduced to the file size */
static int full_dict(long n) { long d = 4096; while (d < n && d < (1L << 29)) d <<= 1; return (int)d; }  /* covers the file, max 512 MiB */
static int ok_roundtrip(const uint8_t *c, long cn, const uint8_t *in, long n) { long m = 0; uint8_t *b = dec(c, cn, &m); int ok = b && m == n && !memcmp(b, in, n); free(b); return ok; }

int main(int argc, char **argv) {
    if (argc != 4 || !strchr("csd", argv[1][0]) || argv[1][1]) { fprintf(stderr, "usage: lzip-hi c|s|d IN OUT\n"); return 2; }
    long n = 0; uint8_t *in = read_file(argv[2], &n); if (!in) return 1;
    if (argv[1][0] == 'd') { long m = 0; uint8_t *o = dec(in, n, &m); if (!o) { fprintf(stderr, "lzip-hi: bad data\n"); return 1; } return write_file(argv[3], o, m) ? 0 : 1; }
    long bn = 0; uint8_t *best = enc(in, n, stock_dict(n), 273, &bn); if (!best) return 1;
    int bd = stock_dict(n), bm = 273;
    if (argv[1][0] == 'c') {
        static const int M[] = {273, 192, 132, 96, 68, 48, 36, 24, 16}; const int NM = 9;
        /* sample: up to 4 evenly spread pieces of 2 MiB (the whole file when it is 8 MiB or less) */
        long piece = 2L << 20, sn = n <= 4 * piece ? n : 4 * piece; uint8_t *s = (uint8_t *)malloc(sn + 1);
        if (sn == n) memcpy(s, in, n); else for (int k = 0; k < 4; k++) memcpy(s + k * piece, in + (n - piece) * k / 3, piece);
        long sz[9]; for (int k = 0; k < NM; k++) { long cn = 0; uint8_t *c = enc(s, sn, full_dict(sn), M[k], &cn); sz[k] = c ? cn : LONG_MAX; free(c); }
        free(s); int a = 0, b2 = -1; for (int k = 1; k < NM; k++) if (sz[k] < sz[a]) a = k;
        for (int k = 0; k < NM; k++) if (k != a && (b2 < 0 || sz[k] < sz[b2])) b2 = k;
        int cand[3] = {M[a], M[b2], 273};
        for (int k = 0; k < 3; k++) {
            if (k == 2 && full_dict(n) == bd) break;            /* same as the stock run already made */
            if (k == 1 && cand[1] == cand[0]) continue;
            long cn = 0; uint8_t *c = enc(in, n, full_dict(n), cand[k], &cn);
            if (c && cn < bn && ok_roundtrip(c, cn, in, n)) { free(best); best = c; bn = cn; bd = full_dict(n); bm = cand[k]; } else free(c);
        }
    }
    if (!ok_roundtrip(best, bn, in, n)) { fprintf(stderr, "lzip-hi: round trip failed\n"); return 1; }
    fprintf(stderr, "lzip-hi: %ld -> %ld bytes (dictionary %d, match length limit %d)\n", n, bn, bd, bm);
    return write_file(argv[3], best, bn) ? 0 : 1;
}
