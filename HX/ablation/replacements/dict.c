/* dict.exe - Dictionary encoding (the Apache Parquet dictionary-encoding idea, without run lengths). Replacement for the HX
   Ablation Test. Values of W bytes; dictionary = distinct values in order of first appearance; each value written as its
   index in max(1, ceil(log2 K)) bits, packed LSB-first. K > 65536: stored raw (flag 0).
   Copyright (c) 2026 Michael Hugh Harrington. MIT License.   dict f IN OUT [-w 1|2|4]   |   dict i IN OUT */
#include "common.inc"
static uint32_t val(const uint8_t *p, int W) { uint32_t v = 0; for (int i = W - 1; i >= 0; i--) v = v << 8 | p[i]; return v; }
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: dict f IN OUT [-w 1|2|4] | dict i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0};
    if (argv[1][0] == 'f') { int W = argval(argc, argv, "-w", 2); if (W != 1 && W != 2 && W != 4) die("width must be 1, 2 or 4");
        header(&o, "DIC1", m); put8(&o, (uint8_t)W); uint64_t e = m / W;
        /* open-addressing table value -> index (size 2^18, enough for 65,537 entries) */
        const uint32_t TS = 1u << 18; uint32_t *key = (uint32_t *)malloc(TS * 4), *idx = (uint32_t *)malloc(TS * 4); memset(idx, 0xff, TS * 4);
        uint32_t *dict = (uint32_t *)malloc(65537 * 4), K = 0; uint32_t *ix = (uint32_t *)malloc((e ? e : 1) * 4); int over = 0;
        for (uint64_t i = 0; i < e && !over; i++) { uint32_t v = val(d + i * W, W), h = (v * 2654435761u) >> 14;
            while (idx[h] != 0xffffffffu && key[h] != v) h = (h + 1) & (TS - 1);
            if (idx[h] == 0xffffffffu) { if (K == 65536) { over = 1; break; } key[h] = v; idx[h] = K; dict[K++] = v; }
            ix[i] = idx[h]; }
        if (over) { put8(&o, 0); put(&o, d, (size_t)m); }
        else { put8(&o, 1); put32(&o, K); for (uint32_t k = 0; k < K; k++) for (int b = 0; b < W; b++) put8(&o, (uint8_t)(dict[k] >> 8 * b));
            int bw = 1; while ((1u << bw) < K) bw++;
            uint64_t acc = 0; int nb = 0; for (uint64_t i = 0; i < e; i++) { acc |= (uint64_t)ix[i] << nb; nb += bw; while (nb >= 8) { put8(&o, (uint8_t)acc); acc >>= 8; nb -= 8; } }
            if (nb) put8(&o, (uint8_t)acc); put(&o, d + e * W, (size_t)(m - e * W)); }
    } else { uint64_t n; const uint8_t *p = check(d, m, "DIC1", &n, 2), *end = d + m; int W = *p++, flag = *p++; if (W != 1 && W != 2 && W != 4) die("damaged file");
        o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; uint64_t e = n / W;
        if (!flag) { if ((uint64_t)(end - p) != n) die("damaged file"); memcpy(o.d, p, n); }
        else { if (end - p < 4) die("damaged file"); uint32_t K = g32(p); p += 4; if (K > 65536 || (uint64_t)(end - p) < (uint64_t)K * W) die("damaged file");
            const uint8_t *dv = p; p += (size_t)K * W; int bw = 1; while ((1u << bw) < K) bw++;
            uint64_t acc = 0; int nb = 0; for (uint64_t i = 0; i < e; i++) { while (nb < bw) { if (p >= end) die("damaged file"); acc |= (uint64_t)*p++ << nb; nb += 8; }
                uint32_t k = (uint32_t)(acc & ((1u << bw) - 1)); acc >>= bw; nb -= bw; if (k >= K) die("damaged file"); memcpy(o.d + i * W, dv + (size_t)k * W, W); }
            if ((uint64_t)(end - p) != n - e * W) die("damaged file"); memcpy(o.d + e * W, p, (size_t)(n - e * W)); } }
    wr(argv[3], o.d, o.n); return 0;
}
