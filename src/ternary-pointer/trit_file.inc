
// ---- hcm-trit v1 file: "HCMT", version 1, swap pattern (2 bytes, base 3),
// flip byte, 32-bit length, CRC-32, then the coded bits.
static const int T_HEADER = 16;
struct Cand { int pat[6]; int xr; };
// patterns found by search on structured binary data, plus plain base 3
static const Cand CANDS[] = {
    {{0, 2, 0, 0, 0, 0}, 255}, {{0, 2, 0, 2, 1, 1}, 255}, {{0, 2, 2, 2, 2, 2}, 255},
    {{0, 1, 2, 1, 1, 1}, 255}, {{0, 0, 0, 0, 0, 0}, 0}};
static const int NCANDS = 5;

static int is_trit(const uint8_t *blob, size_t n) {
    return n >= (size_t)T_HEADER && blob[0] == 'H' && blob[1] == 'C' && blob[2] == 'M' && blob[3] == 'T';
}

static int encode_one(const uint8_t *data, size_t n, const Cand *cd, uint8_t **out, size_t *outn) {
    TTree *tr = (TTree *)malloc(sizeof(TTree));
    Tables *t = (Tables *)malloc(sizeof(Tables));
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!tr || !t || !buf) { free(tr); free(t); free(buf); return -1; }
    if (tables_init(t, 16, 0, n)) { free(tr); free(t); free(buf); return -1; }
    tt_make(tr, cd->pat, cd->xr);
    memcpy(buf, data, n);
    BitWriter w;
    bw_init(&w, n / 3 + 64);
    int pc = 0;
    for (int k = 0; k < 6; k++) pc = pc * 3 + cd->pat[k];
    uint8_t hdr[T_HEADER] = {'H', 'C', 'M', 'T', 1, (uint8_t)(pc >> 8), (uint8_t)pc, (uint8_t)cd->xr};
    put32(hdr + 8, (uint32_t)n);
    put32(hdr + 12, crc32(data, n));
    for (int i = 0; i < T_HEADER; i++) bw_put(&w, hdr[i], 8);
    Coder e = {0, TOP, 0, 0, &w, NULL};
    run<0, 0>(t, tr, buf, n, &e);
    enc_finish(&e);
    bw_flush(&w);
    tables_free(t); free(t); free(buf); free(tr);
    if (w.failed) { free(w.data); return -1; }
    *out = w.data;
    *outn = w.size;
    return 0;
}

// Tries every candidate pattern and keeps the smallest result.
static int compress(const uint8_t *data, size_t n, uint8_t **out, size_t *outn) {
    uint8_t *best = NULL;
    size_t bestn = 0;
    for (int k = 0; k < NCANDS; k++) {
        uint8_t *b;
        size_t bn;
        if (encode_one(data, n, &CANDS[k], &b, &bn)) { free(best); return -1; }
        if (!best || bn < bestn) { free(best); best = b; bestn = bn; }
        else free(b);
    }
    *out = best;
    *outn = bestn;
    return 0;
}

// Returns 0, -1 (memory), -2 (not a valid file), -5 (damaged).
static int decompress(const uint8_t *blob, size_t bn, uint8_t **out, size_t *outn) {
    if (!is_trit(blob, bn) || blob[4] != 1) return -2;
    int pc = (blob[5] << 8) | blob[6];
    if (pc >= 729) return -2;
    Cand cd;
    for (int k = 5; k >= 0; k--) { cd.pat[k] = pc % 3; pc /= 3; }
    cd.xr = blob[7];
    uint32_t n = get32(blob + 8), crc = get32(blob + 12);
    if (n > 0x7FFFFFFFu || (uint64_t)n > ((uint64_t)bn + 16) * 4096) return -2;
    TTree *tr = (TTree *)malloc(sizeof(TTree));
    Tables *t = (Tables *)malloc(sizeof(Tables));
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (!tr || !t || !buf) { free(tr); free(t); free(buf); return -1; }
    if (tables_init(t, 16, 0, n)) { free(tr); free(t); free(buf); return -1; }
    tt_make(tr, cd.pat, cd.xr);
    BitReader r = {blob + T_HEADER, (uint64_t)(bn - T_HEADER) * 8, 0};
    Coder d = {0, TOP, 0, 0, NULL, &r};
    d.v = br_get(&r, 32);
    run<1, 0>(t, tr, buf, n, &d);
    tables_free(t); free(t); free(tr);
    if (crc32(buf, n) != crc) { free(buf); return -5; }
    *out = buf;
    *outn = n;
    return 0;
}
