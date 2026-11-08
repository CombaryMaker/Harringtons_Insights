/* shuffle.exe - Byte shuffle, as the HDF5 "shuffle" filter and Blosc (Francesc Alted). Replacement for the HX Ablation Test.
   Copyright (c) 2026 Michael Hugh Harrington. MIT License.   shuffle f IN OUT [-w 2|4|8]   |   shuffle i IN OUT */
#include "common.inc"
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: shuffle f IN OUT [-w 2|4|8] | shuffle i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0};
    if (argv[1][0] == 'f') { int W = argval(argc, argv, "-w", 2); if (W != 2 && W != 4 && W != 8) die("width must be 2, 4 or 8");
        header(&o, "SHF1", m); put8(&o, (uint8_t)W); uint64_t e = m / W; size_t st = o.n; uint8_t z = 0; for (uint64_t i = 0; i < m; i++) put(&o, &z, 1);
        for (int k = 0; k < W; k++) for (uint64_t i = 0; i < e; i++) o.d[st + k * e + i] = d[i * W + k];
        memcpy(o.d + st + e * W, d + e * W, m - e * W);
    } else { uint64_t n; const uint8_t *p = check(d, m, "SHF1", &n, 1); int W = *p++; if ((W != 2 && W != 4 && W != 8) || m != 14 + n) die("damaged file");
        o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; uint64_t e = n / W;
        for (int k = 0; k < W; k++) for (uint64_t i = 0; i < e; i++) o.d[i * W + k] = p[k * e + i]; memcpy(o.d + e * W, p + e * W, n - e * W); }
    wr(argv[3], o.d, o.n); return 0;
}
