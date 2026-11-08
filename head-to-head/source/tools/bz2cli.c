/* bzip2 front end for the head-to-head app. Michael Hugh Harrington, MIT License, 2026.
 * Built twice against libbz2 (bzip2 1.1, BSD-style licence, Julian Seward):
 *   bzip2-stock.exe  - stock libbz2, block size 9, work factor 30: byte-identical to "bzip2 -9" output
 *   bzip2-hi.exe     - bzip2-hi's tuned compress.c; tries block sizes 9..1 and keeps the smallest (as bz2_hi.py)
 * Both write standard .bz2 and decode with the unmodified libbz2 decoder.
 *   bzip2-xxx c IN OUT     bzip2-xxx d IN OUT */
#include "bzlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *rd(const char *f, unsigned *n) {
    FILE *p = fopen(f, "rb"); if (!p) { perror(f); exit(1); }
    fseek(p, 0, SEEK_END); long s = ftell(p); fseek(p, 0, SEEK_SET);
    char *b = malloc(s ? s : 1); if (fread(b, 1, s, p) != (size_t)s) { perror(f); exit(1); } fclose(p); *n = (unsigned)s; return b;
}
static void wr(const char *f, const char *b, unsigned n) { FILE *p = fopen(f, "wb"); if (!p || fwrite(b, 1, n, p) != n) { perror(f); exit(1); } fclose(p); }
int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "c") && strcmp(argv[1], "d"))) { fprintf(stderr, "usage: %s c IN OUT | %s d IN OUT\n", argv[0], argv[0]); return 2; }
    unsigned n; char *d = rd(argv[2], &n);
    if (argv[1][0] == 'd') {
        unsigned cap = n * 8 + 4096; char *o;
        for (;;) { o = malloc(cap); unsigned m = cap; int r = BZ2_bzBuffToBuffDecompress(o, &m, d, n, 0, 0);
            if (r == BZ_OK) { wr(argv[3], o, m); return 0; }
            free(o); if (r != BZ_OUTBUFF_FULL) { fprintf(stderr, "bzip2: decode error %d\n", r); return 1; } cap *= 2; }
    }
    unsigned cap = n + n / 100 + 600; char *best = malloc(cap), *tmp = malloc(cap); unsigned bn = 0;
#ifdef BZ_HI
    int lo = 1;
#else
    int lo = 9;
#endif
    for (int lvl = 9; lvl >= lo; lvl--) { unsigned m = cap;
        if (BZ2_bzBuffToBuffCompress(tmp, &m, d, n, lvl, 0, 30) != BZ_OK) { fprintf(stderr, "bzip2: encode error\n"); return 1; }
        if (!bn || m < bn) { char *t = best; best = tmp; tmp = t; bn = m; } }
    wr(argv[3], best, bn);
    return 0;
}
