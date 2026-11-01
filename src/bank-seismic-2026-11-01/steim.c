// steim.c - minimal miniSEED 2 reader: decodes Steim1/Steim2 records to int32 samples (little-endian) and checks
// every record's reverse integration constant (Xn). Michael Hugh Harrington, 2026. MIT License.
// usage: steim IN.mseed OUT.i32     prints: records, samples, sample rate, encoding, Xn check failures
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const unsigned char *p) { return p[0] << 8 | p[1]; }
static int32_t sx(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: steim IN.mseed OUT.i32\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"), *o = fopen(argv[2], "wb"); if (!f || !o) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) return 1;
    long pos = 0, recs = 0, samples = 0, bad = 0; int enc = -1; double rate = 0; int32_t *out = malloc(sizeof(int32_t) * 8192);
    while (pos + 48 <= n) {
        unsigned char *r = buf + pos;
        int ns = be16(r + 30), doff = be16(r + 44), boff = be16(r + 46), reclen = 4096;
        int16_t rf = (int16_t)be16(r + 32), rm = (int16_t)be16(r + 34);
        rate = rf > 0 ? (rm > 0 ? rf * rm : -(double)rf / rm) : (rm > 0 ? -(double)rm / rf : 1.0 / (rf * rm));
        while (boff && boff < 4096) { int bt = be16(r + boff); if (bt == 1000) { enc = r[boff + 4]; reclen = 1 << r[boff + 6]; } boff = be16(r + boff + 2); }
        if (enc != 10 && enc != 11) { fprintf(stderr, "unsupported encoding %d\n", enc); return 1; }
        int nf = (reclen - doff) / 64, k = 0; int32_t x0 = 0, xn = 0, last = 0; int first = 1;
        for (int fr = 0; fr < nf && k < ns; fr++) {
            unsigned char *F = r + doff + fr * 64; uint32_t ctrl = be32(F);
            for (int w = 1; w < 16 && k < ns; w++) {
                int c = (ctrl >> (30 - 2 * w)) & 3; uint32_t v = be32(F + 4 * w);
                if (fr == 0 && w == 1) { x0 = (int32_t)v; continue; }
                if (fr == 0 && w == 2) { xn = (int32_t)v; continue; }
                int32_t d[7]; int m = 0;
                if (c == 0) continue;
                if (enc == 10) {
                    if (c == 1) { for (int i = 0; i < 4; i++) d[m++] = (int8_t)(v >> (24 - 8 * i)); }
                    else if (c == 2) { d[m++] = (int16_t)(v >> 16); d[m++] = (int16_t)v; }
                    else d[m++] = (int32_t)v;
                } else {
                    int dn = v >> 30;
                    if (c == 1) { for (int i = 0; i < 4; i++) d[m++] = (int8_t)(v >> (24 - 8 * i)); }
                    else if (c == 2) { if (dn == 1) d[m++] = sx(v, 30); else if (dn == 2) { d[m++] = sx(v >> 15, 15); d[m++] = sx(v, 15); } else if (dn == 3) { for (int i = 0; i < 3; i++) d[m++] = sx(v >> (20 - 10 * i), 10); } }
                    else { if (dn == 0) { for (int i = 0; i < 5; i++) d[m++] = sx(v >> (24 - 6 * i), 6); } else if (dn == 1) { for (int i = 0; i < 6; i++) d[m++] = sx(v >> (25 - 5 * i), 5); } else if (dn == 2) { for (int i = 0; i < 7; i++) d[m++] = sx(v >> (24 - 4 * i), 4); } }
                }
                for (int i = 0; i < m && k < ns; i++) { if (first) { last = x0; first = 0; } else last += d[i]; out[k++] = last; }
            }
        }
        if (k != ns || last != xn) bad++;
        fwrite(out, sizeof(int32_t), k, o); samples += k; recs++; pos += reclen;
    }
    printf("%s: %ld records, %ld samples, %.1f sps, encoding %s, Xn check failures %ld\n", argv[1], recs, samples, rate, enc == 11 ? "Steim2" : "Steim1", bad);
    fclose(o); return bad ? 1 : 0;
}
