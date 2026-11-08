/* histcluster.exe - Byte-histogram clustering ("fileprints": McDaniel & Heydari 2003; Li, Wang, Stolfo & Herzog 2005).
   Replacement for the Group stage of the HX Ablation Test. Each window's 256-bin byte histogram is scaled to counts per
   65,536 bytes; k-means with K = min(4, windows), Manhattan distance, starting centres at windows i*C/K, exactly 10
   iterations (assign, then integer-mean update; an empty cluster keeps its centre), final assignment, classes renumbered by
   first appearance. Integers only. Copyright (c) 2026 Michael Hugh Harrington. MIT License.   histcluster IN MAP [-b WINDOW] */
#include "common.inc"
#include "groupcommon.inc"
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: histcluster IN MAP [-b WINDOW]\n"); return 2; }
    uint64_t n; uint8_t *d = rd(argv[1], &n); uint32_t W = window_arg(argc, argv); uint32_t C = (uint32_t)((n + W - 1) / W);
    uint32_t *h = (uint32_t *)calloc((size_t)(C ? C : 1) * 256, 4); uint8_t *cls = (uint8_t *)calloc(C ? C : 1, 1);
    for (uint32_t w = 0; w < C; w++) { const uint8_t *p = d + (uint64_t)w * W; uint32_t L = (uint32_t)(n - (uint64_t)w * W < W ? n - (uint64_t)w * W : W);
        uint32_t cnt[256] = {0}; for (uint32_t i = 0; i < L; i++) cnt[p[i]]++; for (int b = 0; b < 256; b++) h[(size_t)w * 256 + b] = (uint32_t)((uint64_t)cnt[b] * 65536 / L); }
    uint32_t K = C < 4 ? C : 4; uint64_t cen[4][256];
    for (uint32_t k = 0; k < K; k++) for (int b = 0; b < 256; b++) cen[k][b] = h[(size_t)(k * C / K) * 256 + b];
    for (int it = 0; it <= 10 && K; it++) {
        for (uint32_t w = 0; w < C; w++) { uint64_t best = UINT64_MAX; uint8_t bk = 0;
            for (uint32_t k = 0; k < K; k++) { uint64_t s = 0; for (int b = 0; b < 256; b++) { int64_t df = (int64_t)h[(size_t)w * 256 + b] - (int64_t)cen[k][b]; s += (uint64_t)(df < 0 ? -df : df); }
                if (s < best) { best = s; bk = (uint8_t)k; } }
            cls[w] = bk; }
        if (it == 10) break;
        for (uint32_t k = 0; k < K; k++) { uint64_t sum[256] = {0}, m = 0; for (uint32_t w = 0; w < C; w++) if (cls[w] == k) { m++; for (int b = 0; b < 256; b++) sum[b] += h[(size_t)w * 256 + b]; }
            if (m) for (int b = 0; b < 256; b++) cen[k][b] = sum[b] / m; } }
    int map[4] = {-1, -1, -1, -1}, nx = 0; for (uint32_t w = 0; w < C; w++) { if (map[cls[w]] < 0) map[cls[w]] = nx++; cls[w] = (uint8_t)map[cls[w]]; }
    write_map(argv[2], W, C, cls); return 0;
}
