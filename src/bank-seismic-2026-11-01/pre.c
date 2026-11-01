// pre.c - reversible front ends for the seismic and bank tests. Michael Hugh Harrington, 2026. MIT License.
//   pre ad  IN OUT   int32 first difference, then byte shuffle (4 byte planes)   [standard scientific-data practice]
//   pre uad IN OUT   inverse of ad
//   pre rd  IN OUT   Harrington record-length delta: find the record length R from the spacing between repeats of each
//                    byte value (most frequent spacing wins), then each byte minus the byte R back. 4-byte header = R.
//   pre urd IN OUT   inverse of rd
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned char *rd(const char *f, long *n) { FILE *p = fopen(f, "rb"); if (!p) { perror(f); exit(1); } fseek(p, 0, SEEK_END); *n = ftell(p); fseek(p, 0, SEEK_SET); unsigned char *b = malloc(*n + 16); if (fread(b, 1, *n, p) != (size_t)*n) exit(1); fclose(p); return b; }
static void wr(const char *f, const void *b, long n) { FILE *p = fopen(f, "wb"); fwrite(b, 1, n, p); fclose(p); }
int main(int argc, char **argv) {
    if (argc < 4) return 2; long n; unsigned char *b = rd(argv[2], &n); unsigned char *o = malloc(n + 16);
    if (!strcmp(argv[1], "ad") || !strcmp(argv[1], "uad")) {
        long m = n / 4, tail = n - m * 4; int32_t *x = (int32_t *)b;
        if (argv[1][0] == 'a') {
            int32_t prev = 0; for (long i = 0; i < m; i++) { int32_t d = x[i] - prev; prev = x[i]; for (int k = 0; k < 4; k++) o[k * m + i] = (unsigned char)((uint32_t)d >> (8 * k)); }
        } else {
            int32_t prev = 0, *y = (int32_t *)o; for (long i = 0; i < m; i++) { uint32_t d = 0; for (int k = 0; k < 4; k++) d |= (uint32_t)b[k * m + i] << (8 * k); prev += (int32_t)d; y[i] = prev; }
        }
        memcpy(o + m * 4, b + m * 4, tail); wr(argv[3], o, n); return 0;
    }
    if (!strcmp(argv[1], "rd")) {
        static long last[256], hist[4097]; for (int i = 0; i < 256; i++) last[i] = -1;
        for (long i = 0; i < n; i++) { long l = last[b[i]]; if (l >= 0 && i - l <= 4096) hist[i - l]++; last[b[i]] = i; }
        long R = 1, best = -1; for (long r = 2; r <= 4096; r++) if (hist[r] > best) { best = hist[r]; R = r; }   // spacing 1 is plain repetition, not a record
        uint32_t R32 = (uint32_t)R; memcpy(o, &R32, 4);
        for (long i = 0; i < n; i++) o[4 + i] = b[i] - (i >= R ? b[i - R] : 0);
        wr(argv[3], o, n + 4); fprintf(stderr, "record length %ld\n", R); return 0;
    }
    if (!strcmp(argv[1], "urd")) {
        uint32_t R; memcpy(&R, b, 4); long m = n - 4;
        for (long i = 0; i < m; i++) o[i] = b[4 + i] + (i >= (long)R ? o[i - R] : 0);
        wr(argv[3], o, m); return 0;
    }
    return 2;
}
