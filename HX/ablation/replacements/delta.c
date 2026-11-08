/* delta.exe - Byte delta, as the XZ Utils / 7-Zip Delta filter. Replacement for the HX Ablation Test.
   Copyright (c) 2026 Michael Hugh Harrington. MIT License.   delta f IN OUT [-d 1..256]   |   delta i IN OUT */
#include "common.inc"
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: delta f IN OUT [-d 1..256] | delta i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0};
    if (argv[1][0] == 'f') { int D = argval(argc, argv, "-d", 1); if (D < 1 || D > 256) die("distance must be 1..256");
        header(&o, "DLT1", m); put16(&o, (uint16_t)D); size_t st = o.n; put(&o, d, (size_t)m);
        for (uint64_t i = m; i-- > (uint64_t)D;) o.d[st + i] = (uint8_t)(d[i] - d[i - D]);
    } else { uint64_t n; const uint8_t *p = check(d, m, "DLT1", &n, 2); int D = g16(p); p += 2; if (D < 1 || D > 256 || m != 15 + n) die("damaged file");
        o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; memcpy(o.d, p, n); for (uint64_t i = D; i < n; i++) o.d[i] = (uint8_t)(o.d[i] + o.d[i - D]); }
    wr(argv[3], o.d, o.n); return 0;
}
