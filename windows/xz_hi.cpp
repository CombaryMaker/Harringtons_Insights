// xz-hi: smaller standard .xz files (Harrington Insights).
// Compressor-side search only: per file it tries LZMA2 lc/lp/pb, match effort
// (nice_len, depth), an optional pre-filter (delta at the record length that
// HCM's record detector finds, or a BCJ filter for executables), a dictionary
// sized to the file and a CRC32 check, and keeps the smallest result. Any xz
// decodes the output. Uses liblzma from XZ Utils (0BSD / public domain).
// MIT License, Copyright (c) 2026 Michael Harrington.
#define LZMA_API_STATIC
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int looks_binary(const uint8_t *d, size_t n) {
    size_t ctl = 0;
    for (size_t i = 0; i < n; i++) { unsigned c = d[i]; ctl += c < 9 || (c > 13 && c < 32) || c == 127; }
    return n > 0 && ctl * 10 > n;
}
// HCM's record detector: a byte value seen at equal spacing three times in a
// row votes for that spacing; enough votes fix the record length.
static int record_length(const uint8_t *d, size_t n) {
    static int32_t lp[256][3];
    for (int c = 0; c < 256; c++) lp[c][0] = lp[c][1] = lp[c][2] = -1;
    int cand = 0, cnt = 0, R = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t p = (int32_t)i, *l = lp[d[i]];
        int d1 = l[0] >= 0 ? p - l[0] : 0, d2 = l[1] >= 0 ? l[0] - l[1] : -1, d3 = l[2] >= 0 ? l[1] - l[2] : -2;
        if (d1 > 1 && d1 == d2 && d1 == d3 && d1 < 65536) {
            if (d1 == cand) { if (cnt < 64) cnt++; }
            else if (cnt > 0) cnt--;
            else { cand = d1; cnt = 1; }
            if (cnt >= 3 && cand != R) R = cand;
        }
        l[2] = l[1]; l[1] = l[0]; l[0] = p;
    }
    return R;
}
struct Pre { lzma_vli id; uint32_t dist; };
// pre.id == LZMA_VLI_UNKNOWN means no pre-filter
static size_t encode(const uint8_t *in, size_t n, int lc, int lp, int pb, Pre pre, uint32_t depth, uint32_t nice, uint8_t *out, size_t cap) {
    lzma_options_lzma o;
    if (lzma_lzma_preset(&o, 9 | LZMA_PRESET_EXTREME)) return 0;
    o.dict_size = n < 4096 ? 4096 : (uint32_t)n;
    o.lc = lc; o.lp = lp; o.pb = pb; o.depth = depth; o.nice_len = nice; o.mf = LZMA_MF_BT4;
    lzma_options_delta dopt; memset(&dopt, 0, sizeof dopt); dopt.type = LZMA_DELTA_TYPE_BYTE; dopt.dist = pre.dist;
    lzma_filter f[3]; int k = 0;
    if (pre.id != LZMA_VLI_UNKNOWN) { f[k].id = pre.id; f[k].options = pre.id == LZMA_FILTER_DELTA ? (void *)&dopt : NULL; k++; }
    f[k].id = LZMA_FILTER_LZMA2; f[k].options = &o; k++;
    f[k].id = LZMA_VLI_UNKNOWN; f[k].options = NULL;
    lzma_stream st = LZMA_STREAM_INIT;
    if (lzma_stream_encoder(&st, f, LZMA_CHECK_CRC32) != LZMA_OK) return 0;
    st.next_in = in; st.avail_in = n; st.next_out = out; st.avail_out = cap;
    lzma_ret rc = lzma_code(&st, LZMA_FINISH);
    size_t pos = cap - st.avail_out;
    lzma_end(&st);
    return rc == LZMA_STREAM_END ? pos : 0;
}
static const char *TOOL_NAME = "xz-hi", *TOOL_EXT = ".xz", *TOOL_BASELINE = "xz -9e";
static int tool_compress(const uint8_t *in, size_t n, uint8_t **out, size_t *outn, size_t *basen, char *note, size_t notesz) {
    size_t cap = lzma_stream_buffer_bound(n);
    uint8_t *best = (uint8_t *)malloc(cap), *tmp = (uint8_t *)malloc(cap);
    if (!best || !tmp) { free(best); free(tmp); return -1; }
    size_t bn = 0;
    if (lzma_easy_buffer_encode(9 | LZMA_PRESET_EXTREME, LZMA_CHECK_CRC64, NULL, in, n, best, &bn, cap) != LZMA_OK) { free(best); free(tmp); return -1; }
    *basen = bn;
    snprintf(note, notesz, "settings: same as xz -9e");
    struct LC { size_t sz; int lc, lp, pb; } c[64]; int nc = 0;
    Pre none = {LZMA_VLI_UNKNOWN, 0};
    for (int lc = 0; lc <= 4; lc++) for (int lp = 0; lp <= 2; lp++) for (int pb = 0; pb <= 2; pb++) {
        if (lc + lp > 4) continue;
        size_t s = encode(in, n, lc, lp, pb, none, 512, 273, tmp, cap);
        if (s) { c[nc].sz = s; c[nc].lc = lc; c[nc].lp = lp; c[nc].pb = pb; nc++; }
    }
    for (int i = 1; i < nc; i++) for (int j = i; j > 0 && c[j].sz < c[j - 1].sz; j--) { LC t = c[j]; c[j] = c[j - 1]; c[j - 1] = t; }
    Pre pres[32]; int np = 0;
    pres[np++] = none;
    if (looks_binary(in, n)) {
        uint32_t dists[16] = {1, 2, 4, 8, 12, 13, 16, 26, 48, 216}; int nd = 10;
        int r = record_length(in, n);
        int have = 0; for (int i = 0; i < nd; i++) have |= (int)dists[i] == r;
        if (r > 0 && r <= 256 && !have) dists[nd++] = (uint32_t)r;
        for (int i = 0; i < nd; i++) { pres[np].id = LZMA_FILTER_DELTA; pres[np].dist = dists[i]; np++; }
        lzma_vli bcj[6] = {LZMA_FILTER_X86, LZMA_FILTER_SPARC, LZMA_FILTER_POWERPC, LZMA_FILTER_ARM, LZMA_FILTER_ARMTHUMB, LZMA_FILTER_IA64};
        for (int i = 0; i < 6; i++) { pres[np].id = bcj[i]; pres[np].dist = 0; np++; }
    }
    static const uint32_t depths[3] = {512, 0, 4000}, nices[2] = {273, 128};
    // stage 2: pre-filters with the four best lc/lp/pb; stage 3: match effort for the winner
    int bt = -1, bpi = 0;
    for (int t = 0; t < 4 && t < nc; t++)
        for (int pi = 0; pi < np; pi++) {
            size_t s = encode(in, n, c[t].lc, c[t].lp, c[t].pb, pres[pi], 512, 273, tmp, cap);
            if (s && s < bn) { uint8_t *x = best; best = tmp; tmp = x; bn = s; bt = t; bpi = pi; }
        }
    int bdi = 0, bni = 0;
    if (bt >= 0)
        for (int di = 0; di < 3; di++)
            for (int ni = 0; ni < 2; ni++) {
                if (di == 0 && ni == 0) continue;
                size_t s = encode(in, n, c[bt].lc, c[bt].lp, c[bt].pb, pres[bpi], depths[di], nices[ni], tmp, cap);
                if (s && s < bn) { uint8_t *x = best; best = tmp; tmp = x; bn = s; bdi = di; bni = ni; }
            }
    if (bt >= 0) {
        lzma_vli id = pres[bpi].id;
        const char *pn = id == LZMA_VLI_UNKNOWN ? "none" : id == LZMA_FILTER_X86 ? "x86" : id == LZMA_FILTER_SPARC ? "sparc" :
                         id == LZMA_FILTER_POWERPC ? "powerpc" : id == LZMA_FILTER_ARM ? "arm" : id == LZMA_FILTER_ARMTHUMB ? "armthumb" : "ia64";
        if (id == LZMA_FILTER_DELTA)
            snprintf(note, notesz, "settings: lc=%d lp=%d pb=%d nice=%u depth=%u, delta filter distance %u (record length)",
                     c[bt].lc, c[bt].lp, c[bt].pb, nices[bni], depths[bdi], pres[bpi].dist);
        else
            snprintf(note, notesz, "settings: lc=%d lp=%d pb=%d nice=%u depth=%u, pre-filter %s",
                     c[bt].lc, c[bt].lp, c[bt].pb, nices[bni], depths[bdi], pn);
    }
    free(tmp);
    *out = best; *outn = bn;
    return 0;
}
static int tool_decompress(const uint8_t *in, size_t n, uint8_t **out, size_t *outn) {
    // the uncompressed size is stored in the .xz index
    lzma_stream_flags fl;
    if (n < 2 * LZMA_STREAM_HEADER_SIZE) return -2;
    if (lzma_stream_footer_decode(&fl, in + n - LZMA_STREAM_HEADER_SIZE) != LZMA_OK) return -2;
    if (fl.backward_size > n - 2 * LZMA_STREAM_HEADER_SIZE) return -2;
    lzma_index *idx = NULL; uint64_t memlimit = UINT64_MAX; size_t ip = 0;
    if (lzma_index_buffer_decode(&idx, &memlimit, NULL, in + n - LZMA_STREAM_HEADER_SIZE - fl.backward_size, &ip, fl.backward_size) != LZMA_OK) return -2;
    uint64_t usize = lzma_index_uncompressed_size(idx);
    lzma_index_end(idx, NULL);
    if (usize > ((uint64_t)1 << 31)) return -3;
    uint8_t *buf = (uint8_t *)malloc(usize ? usize : 1);
    if (!buf) return -4;
    size_t inpos = 0, outpos = 0; memlimit = UINT64_MAX;
    if (lzma_stream_buffer_decode(&memlimit, 0, NULL, in, &inpos, n, buf, &outpos, usize ? usize : 1) != LZMA_OK || outpos != usize) { free(buf); return -2; }
    *out = buf; *outn = outpos;
    return 0;
}

#include "dropframe.h"
