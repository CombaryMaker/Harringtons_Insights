/* bitshuffle.exe - Bitshuffle (Kiyoshi Masui et al., 2015; github.com/kiyo-masui/bitshuffle, MIT). Replacement for the HX
   Ablation Test. Blocks of 8192 elements; inside a block the bit planes are written in order (byte k, bit b), each plane
   packed LSB-first. Copyright (c) 2026 Michael Hugh Harrington. MIT License.
   bitshuffle f IN OUT [-w 1|2|4|8]   |   bitshuffle i IN OUT */
#include "common.inc"
static void planes(const uint8_t *in, uint8_t *out, uint64_t E, int W, int fwd) {   /* E elements, E multiple of 8 */
    size_t pl = (size_t)(E / 8); if (fwd) memset(out, 0, (size_t)(E * W)); else memset(out, 0, (size_t)(E * W));
    for (int k = 0; k < W; k++) for (int b = 0; b < 8; b++) { size_t base = (size_t)(k * 8 + b) * pl;
        for (uint64_t i = 0; i < E; i++) {
            if (fwd) { if ((in[i * W + k] >> b) & 1) out[base + i / 8] |= (uint8_t)(1 << (i % 8)); }
            else { if ((in[base + i / 8] >> (i % 8)) & 1) out[i * W + k] |= (uint8_t)(1 << b); } } }
}
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: bitshuffle f IN OUT [-w 1|2|4|8] | bitshuffle i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0}; int fwd = argv[1][0] == 'f'; const uint8_t *src; uint64_t n; int W;
    if (fwd) { W = argval(argc, argv, "-w", 2); if (W != 1 && W != 2 && W != 4 && W != 8) die("width must be 1, 2, 4 or 8"); header(&o, "BSH1", m); put8(&o, (uint8_t)W); n = m; src = d; }
    else { src = check(d, m, "BSH1", &n, 1); W = *src++; if ((W != 1 && W != 2 && W != 4 && W != 8) || m != 14 + n) die("damaged file"); }
    size_t st = o.n; uint8_t *dst; if (fwd) { uint8_t z = 0; for (uint64_t i = 0; i < n; i++) put(&o, &z, 1); dst = o.d + st; } else { o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; dst = o.d; }
    uint64_t e = n / W, pos = 0;
    while (e - pos >= 8) { uint64_t E = e - pos; if (E > 8192) E = 8192; E -= E % 8; planes(src + pos * W, dst + pos * W, E, W, fwd); pos += E; }
    memcpy(dst + pos * W, src + pos * W, (size_t)(n - pos * W));   /* left-over elements and n mod W bytes: raw */
    wr(argv[3], o.d, o.n); return 0;
}
