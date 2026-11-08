/* dedup.exe - Block deduplication with content-defined chunking (FastCDC: Wen Xia et al., USENIX ATC 2016). Replacement for
   the HX Ablation Test. Gear rolling hash, chunks min 2 KiB / normal 8 KiB / max 64 KiB, normalised chunking level 1 (a mask
   with 14 one-bits before the normal size, 12 after; masks use the high bits of the hash). Gear table from the 64-bit
   xorshift* generator seeded with 1. A chunk is a duplicate only after a full byte-for-byte comparison.
   Copyright (c) 2026 Michael Hugh Harrington. MIT License.   dedup f IN OUT   |   dedup i IN OUT */
#include "common.inc"
static uint64_t xs = 1; static uint64_t xsn(void) { xs ^= xs >> 12; xs ^= xs << 25; xs ^= xs >> 27; return xs * 2685821657736338717ull; }
static uint64_t fnv(const uint8_t *p, size_t n) { uint64_t h = 14695981039346656037ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: dedup f IN OUT | dedup i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0};
    if (argv[1][0] == 'f') {
        uint64_t G[256]; for (int i = 0; i < 256; i++) G[i] = xsn();
        const uint64_t MS = ((1ull << 14) - 1) << 50, ML = ((1ull << 12) - 1) << 52; const size_t MIN = 2048, NORM = 8192, MAX = 65536;
        size_t cap = 1024, cnt = 0; uint64_t *off = (uint64_t *)malloc(cap * 8); uint32_t *len = (uint32_t *)malloc(cap * 4);
        for (uint64_t pos = 0; pos < m;) { uint64_t rem = m - pos; size_t L;
            if (rem <= MIN) L = (size_t)rem;
            else { uint64_t h = 0; size_t i = MIN, lim = rem < MAX ? (size_t)rem : MAX, nl = NORM < lim ? NORM : lim; L = lim;
                for (; i < nl; i++) { h = (h << 1) + G[d[pos + i]]; if (!(h & MS)) { L = i + 1; goto cut; } }
                for (; i < lim; i++) { h = (h << 1) + G[d[pos + i]]; if (!(h & ML)) { L = i + 1; goto cut; } } }
            cut: if (cnt == cap) { cap *= 2; off = (uint64_t *)realloc(off, cap * 8); len = (uint32_t *)realloc(len, cap * 4); }
            off[cnt] = pos; len[cnt] = (uint32_t)L; cnt++; pos += L; }
        /* duplicate search: hash table of chunk hashes -> first chunk with that hash and content */
        size_t TS = 1; while (TS < cnt * 2 + 2) TS <<= 1; int64_t *tab = (int64_t *)malloc(TS * 8); for (size_t i = 0; i < TS; i++) tab[i] = -1;
        uint64_t *hs = (uint64_t *)malloc((cnt ? cnt : 1) * 8); Buf hdr = {0}, body = {0};
        header(&o, "DDP1", m); put32(&hdr, (uint32_t)cnt);
        for (size_t c = 0; c < cnt; c++) { uint64_t h = fnv(d + off[c], len[c]); hs[c] = h; size_t s = (size_t)(h & (TS - 1)); int64_t found = -1;
            while (tab[s] >= 0) { size_t e = (size_t)tab[s]; if (hs[e] == h && len[e] == len[c] && !memcmp(d + off[e], d + off[c], len[c])) { found = (int64_t)e; break; } s = (s + 1) & (TS - 1); }
            if (found >= 0) { put8(&hdr, 1); put32(&hdr, (uint32_t)found); }
            else { tab[s] = (int64_t)c; put8(&hdr, 0); put32(&hdr, len[c]); put(&body, d + off[c], len[c]); } }
        put(&o, hdr.d, hdr.n); if (body.n) put(&o, body.d, body.n);
    } else { uint64_t n; const uint8_t *p = check(d, m, "DDP1", &n, 4), *end = d + m; uint32_t cnt = g32(p); p += 4;
        if ((uint64_t)(end - p) < (uint64_t)cnt * 5) die("damaged file"); const uint8_t *tbl = p, *bp = p + (size_t)cnt * 5;
        uint64_t *off = (uint64_t *)malloc((cnt ? cnt : 1) * 8); uint32_t *len = (uint32_t *)malloc((cnt ? cnt : 1) * 4);
        o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; uint64_t pos = 0;
        for (uint32_t c = 0; c < cnt; c++) { int kind = tbl[c * 5]; uint32_t v = g32(tbl + c * 5 + 1);
            if (kind == 0) { if ((uint64_t)(end - bp) < v || pos + v > n) die("damaged file"); memcpy(o.d + pos, bp, v); bp += v; off[c] = pos; len[c] = v; pos += v; }
            else { if (v >= c || pos + len[v] > n) die("damaged file"); memcpy(o.d + pos, o.d + off[v], len[v]); off[c] = pos; len[c] = len[v]; pos += len[v]; } }
        if (pos != n || bp != end) die("damaged file"); }
    wr(argv[3], o.d, o.n); return 0;
}
