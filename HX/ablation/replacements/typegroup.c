/* typegroup.exe - File-type grouping (archivers' "group by type", e.g. 7-Zip / RAR solid archives). Replacement for the
   Group stage of the HX Ablation Test. Each window is labelled: 2 random (order-0 entropy > 7.5 bits/byte), 0 text (at least
   90% printable ASCII or valid UTF-8), 1 program ("MZ" at file start, or E8/E9 opcodes > 0.5%), 3 other - tested in the
   order 2, 0, 1, 3. Copyright (c) 2026 Michael Hugh Harrington. MIT License.   typegroup IN MAP [-b WINDOW] */
#include "common.inc"
#include "groupcommon.inc"
static int utf8len(const uint8_t *p, const uint8_t *e) {   /* length of a valid multibyte UTF-8 sequence at p, or 0 */
    int n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0; if (!n || p + n > e || *p < 0xC2 || *p > 0xF4) return 0;
    for (int i = 1; i < n; i++) if ((p[i] & 0xC0) != 0x80) return 0; return n;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: typegroup IN MAP [-b WINDOW]\n"); return 2; }
    uint64_t n; uint8_t *d = rd(argv[1], &n); uint32_t W = window_arg(argc, argv); uint32_t C = (uint32_t)((n + W - 1) / W);
    uint8_t *cls = (uint8_t *)malloc(C ? C : 1);
    for (uint32_t w = 0; w < C; w++) { const uint8_t *p = d + (uint64_t)w * W; uint32_t L = (uint32_t)(n - (uint64_t)w * W < W ? n - (uint64_t)w * W : W);
        uint32_t cnt[256] = {0}; for (uint32_t i = 0; i < L; i++) cnt[p[i]]++;
        uint64_t S = 0; for (int b = 0; b < 256; b++) if (cnt[b]) S += (uint64_t)cnt[b] * lg16(cnt[b]);
        uint64_t H = (uint64_t)L * lg16(L) - S;                       /* = entropy * L * 65536 */
        if (H * 2 > (uint64_t)15 * L * 65536) { cls[w] = 2; continue; }
        uint32_t text = 0; for (uint32_t i = 0; i < L;) { uint8_t c = p[i]; if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126)) { text++; i++; continue; }
            int u = c >= 0x80 ? utf8len(p + i, p + L) : 0; if (u) { text += u; i += u; } else i++; }
        if ((uint64_t)text * 10 >= (uint64_t)L * 9) { cls[w] = 0; continue; }
        if ((w == 0 && L >= 2 && p[0] == 'M' && p[1] == 'Z') || (uint64_t)(cnt[0xE8] + cnt[0xE9]) * 200 > L) { cls[w] = 1; continue; }
        cls[w] = 3; }
    write_map(argv[2], W, C, cls); return 0;
}
