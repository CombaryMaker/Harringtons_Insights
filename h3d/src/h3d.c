/* h3d - lossless 3D compressor for DICOM slice series (CT/MRI volumes).
   Copyright (c) 2026 Michael Hugh Harrington. MIT License (see LICENSE.txt).
   Proof of concept, version 1.0 (13 November 2026).

   Each slice is predicted from the slice before it as well as from its own row: seven simple predictors (left/up
   median, 3D median using the slice in front, the slice in front plus the 2D change, the slice in front, gradient,
   average, up) are blended with weights from how wrong each was on the neighbouring, already decoded pixels.
   The error is coded with an adaptive binary arithmetic coder whose context is how large the errors were around the
   pixel, in this slice and in the slice in front, and how much the predictors disagree. DICOM headers are coded with
   the matching header of the previous file as context. All arithmetic is integer, so every computer decodes the same.

   Usage:  h3d c <output.h3d> <file1.dcm> <file2.dcm> ...   (files in slice order)
           h3d d <input.h3d> <output-folder>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ---------- binary arithmetic coder (32-bit, 16-bit probabilities, adaptive shift 5) ---------- */
typedef struct { uint32_t lo, hi, x; uint8_t *buf; size_t pos, cap; } AC;
static void ac_grow(AC *a) { if (a->pos + 8 > a->cap) { a->cap = a->cap * 2 + 1024; a->buf = realloc(a->buf, a->cap); } }
static void enc_bit(AC *a, int bit, uint16_t *p) {
  uint32_t mid = a->lo + (uint32_t)(((uint64_t)(a->hi - a->lo) * *p) >> 16);
  if (bit) { a->hi = mid; *p += (65536 - *p) >> 5; } else { a->lo = mid + 1; *p -= *p >> 5; }
  while (((a->lo ^ a->hi) & 0xff000000) == 0) { ac_grow(a); a->buf[a->pos++] = a->hi >> 24; a->lo <<= 8; a->hi = (a->hi << 8) | 255; }
}
static void enc_flush(AC *a) { for (int i = 0; i < 4; i++) { ac_grow(a); a->buf[a->pos++] = a->lo >> 24; a->lo <<= 8; } }
static int dec_bit(AC *a, uint16_t *p) {
  uint32_t mid = a->lo + (uint32_t)(((uint64_t)(a->hi - a->lo) * *p) >> 16); int bit = a->x <= mid;
  if (bit) { a->hi = mid; *p += (65536 - *p) >> 5; } else { a->lo = mid + 1; *p -= *p >> 5; }
  while (((a->lo ^ a->hi) & 0xff000000) == 0) { a->lo <<= 8; a->hi = (a->hi << 8) | 255; a->x = (a->x << 8) | (a->pos < a->cap ? a->buf[a->pos] : 0); a->pos++; }
  return bit;
}
static void dec_init(AC *a, uint8_t *b, size_t n) { a->lo = 0; a->hi = 0xffffffff; a->x = 0; a->buf = b; a->pos = 0; a->cap = n;
  for (int i = 0; i < 4; i++) { a->x = (a->x << 8) | (a->pos < n ? b[a->pos] : 0); a->pos++; } }

/* ---------- error coder: bit length of the zigzag value (unary), then the bits below the top bit ---------- */
#define NCTX 1728
typedef struct { uint16_t len[NCTX][18], top[NCTX][18][2], low[18][18]; } ErrModel;
static void em_init(ErrModel *m) { uint16_t *p = (uint16_t *)m; for (size_t i = 0; i < sizeof(*m) / 2; i++) p[i] = 32768; }
static void put_err(AC *a, ErrModel *m, int k, int32_t e) {
  uint32_t z = e >= 0 ? 2u * (uint32_t)e : 2u * (uint32_t)(-(int64_t)e) - 1; int b = 0; while (b < 17 && (z >> b)) b++;
  for (int t = 0; t < b; t++) enc_bit(a, 1, &m->len[k][t]);
  if (b < 17) enc_bit(a, 0, &m->len[k][b]);
  for (int t = b - 2; t >= 0; t--) { int pos = b - 2 - t, bit = (z >> t) & 1; enc_bit(a, bit, pos < 2 ? &m->top[k][b][pos] : &m->low[b][pos]); }
}
static int32_t get_err(AC *a, ErrModel *m, int k) {
  int b = 0; while (b < 17 && dec_bit(a, &m->len[k][b])) b++;
  uint32_t z = b ? 1u << (b - 1) : 0;
  for (int t = b - 2; t >= 0; t--) { int pos = b - 2 - t; if (dec_bit(a, pos < 2 ? &m->top[k][b][pos] : &m->low[b][pos])) z |= 1u << t; }
  return (z & 1) ? -(int32_t)((z + 1) >> 1) : (int32_t)(z >> 1);
}

/* ---------- pixel model ---------- */
static inline int q11(int64_t x) { if (x < 0) x = -x; int b = 0; x += 1; while (x > 1 && b < 11) { x >>= 1; b++; } return b; }
static inline int64_t med3(int64_t a, int64_t b, int64_t c) { int64_t mx = a > b ? a : b, mn = a < b ? a : b; return c >= mx ? mn : c <= mn ? mx : a + b - c; }
static inline int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
#define NP 7
/* codes (dec=0) or decodes (dec=1) one slice; cur = this slice, prv = slice in front (zeros for the first),
   E = per-predictor errors of this slice (out), Ep = of the slice in front, EW / EWp = blended errors */
static void slice(AC *a, ErrModel *m, int dec, int W, int H, int32_t *cur, const int32_t *prv, int32_t *E, const int32_t *Ep, int32_t *EW, const int32_t *EWp) {
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    long i = (long)y * W + x;
    int64_t w = x ? cur[i - 1] : 0, n = y ? cur[i - W] : 0, nw = (x && y) ? cur[i - W - 1] : 0, ne = (y && x < W - 1) ? cur[i - W + 1] : 0;
    int64_t f = prv[i], wf = x ? prv[i - 1] : 0, nf = y ? prv[i - W] : 0, nwf = (x && y) ? prv[i - W - 1] : 0;
    int64_t P[NP] = { med3(w, n, nw), med3(w, f, wf), f + med3(w - wf, n - nf, nw - nwf), f, w + n - nw, fdiv(w + ne, 2), n };
    int64_t num = 0, den = 0;
    for (int k = 0; k < NP; k++) {
      const int32_t *Ek = E + (long)k * W * H; int64_t s = 1;
      if (x) s += llabs(Ek[i - 1]);
      if (y) s += llabs(Ek[i - W]);
      if (x && y) s += llabs(Ek[i - W - 1]);
      if (y && x < W - 1) s += llabs(Ek[i - W + 1]);
      if (k == 1 || k == 2 || k == 3) s += llabs(Ep[(long)k * W * H + i]);
      if (s > 65535) s = 65535;
      int64_t wt = ((int64_t)1 << 32) / (s * s); num += wt * P[k]; den += wt;
    }
    int64_t pred = fdiv(2 * num + den, 2 * den);
    int64_t aa = (x ? llabs(EW[i - 1]) : 0) + (y ? llabs(EW[i - W]) : 0) + (((x && y) ? llabs(EW[i - W - 1]) : 0) + ((y && x < W - 1) ? llabs(EW[i - W + 1]) : 0)) / 2;
    int64_t bb = llabs(EWp[i]) + ((x ? llabs(EWp[i - 1]) : 0) + (y ? llabs(EWp[i - W]) : 0)) / 2;
    int k = (q11(aa) * 12 + q11(bb)) * 12 + q11(llabs(P[0] - P[1]) + llabs(P[2] - P[1]));
    if (dec) cur[i] = (int32_t)(get_err(a, m, k) + pred); else put_err(a, m, k, (int32_t)(cur[i] - pred));
    EW[i] = (int32_t)(cur[i] - pred);
    for (int j = 0; j < NP; j++) E[(long)j * W * H + i] = (int32_t)(cur[i] - P[j]);
  }
}

/* ---------- header bytes: context = previous byte + byte at the same place in the previous header ---------- */
static uint16_t *HM;
static void hdr_code(AC *a, int dec, uint8_t *b, size_t n, const uint8_t *prev, size_t pn) {
  uint8_t c1 = 0;
  for (size_t i = 0; i < n; i++) {
    int ctx = (i < pn ? prev[i] : 0) << 8 | c1; uint16_t *p = HM + (size_t)ctx * 256; int node = 1;
    if (dec) { while (node < 256) node = node * 2 + dec_bit(a, &p[node]); b[i] = node & 255; }
    else { for (int t = 7; t >= 0; t--) { int bit = (b[i] >> t) & 1; enc_bit(a, bit, &p[node]); node = node * 2 + bit; } }
    c1 = b[i];
  }
}

/* ---------- DICOM: find 16-bit greyscale pixel data and its size ---------- */
static uint32_t g16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t g32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int find_tag16(const uint8_t *d, size_t n, size_t lim, uint16_t g, uint16_t e, uint32_t *val) {
  for (size_t i = 132; i + 10 < lim && i + 10 < n; i++)
    if (g16(d + i) == g && g16(d + i + 2) == e) {
      if (d[i + 4] == 'U' && d[i + 5] == 'S' && g16(d + i + 6) == 2) { *val = g16(d + i + 8); return 1; }
      if (g32(d + i + 4) == 2) { *val = g16(d + i + 8); return 1; }
    }
  return 0;
}
/* returns pixel offset (0 = not a plain 16-bit greyscale DICOM) */
static size_t dicom_px(const uint8_t *d, size_t n, int *W, int *H, int *sgn) {
  if (n < 140 || memcmp(d + 128, "DICM", 4)) return 0;
  uint32_t r, c, ba, pr = 0, spp = 1;
  for (size_t i = n >= 12 ? n - 12 : 0; ; i--) {   /* pixel data is the last big element: search from the end */
    if (g16(d + i) == 0x7FE0 && g16(d + i + 2) == 0x0010) {
      size_t off; uint32_t len;
      if (d[i + 4] == 'O' && (d[i + 5] == 'W' || d[i + 5] == 'B')) { len = g32(d + i + 8); off = i + 12; } else { len = g32(d + i + 4); off = i + 8; }
      if (!find_tag16(d, n, i, 0x0028, 0x0010, &r) || !find_tag16(d, n, i, 0x0028, 0x0011, &c) || !find_tag16(d, n, i, 0x0028, 0x0100, &ba)) return 0;
      find_tag16(d, n, i, 0x0028, 0x0103, &pr); find_tag16(d, n, i, 0x0028, 0x0002, &spp);
      if (ba != 16 || spp != 1 || len != (uint64_t)r * c * 2 || off + len > n) return 0;
      *W = c; *H = r; *sgn = pr == 1; return off;
    }
    if (i == 132) return 0;
  }
}

static uint8_t *readf(const char *fn, size_t *n) { FILE *f = fopen(fn, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return 0; } fclose(f); return b; }
static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static uint32_t get32f(const uint8_t **p) { uint32_t v = g32(*p); *p += 4; return v; }
static const char *base(const char *s) { const char *b = s; for (; *s; s++) if (*s == '/' || *s == '\\') b = s + 1; return b; }

int main(int argc, char **argv) {
  if (argc < 4 || (argv[1][0] != 'c' && argv[1][0] != 'd')) { fprintf(stderr, "h3d 1.0 (c) 2026 Michael Hugh Harrington, MIT\n  h3d c out.h3d slice1.dcm slice2.dcm ...\n  h3d d in.h3d out-folder\n"); return 1; }
  HM = malloc(sizeof(uint16_t) * 65536 * 256); for (size_t i = 0; i < 65536u * 256; i++) HM[i] = 32768;
  ErrModel *m = malloc(sizeof(ErrModel)); em_init(m);
  clock_t t0 = clock();
  if (argv[1][0] == 'c') {
    int nf = argc - 3; AC a = { 0, 0xffffffff, 0, 0, 0, 0 };
    FILE *o = fopen(argv[2], "wb"); if (!o) { perror(argv[2]); return 1; }
    fwrite("H3D1", 1, 4, o); put32(o, nf);
    uint8_t **D = malloc(nf * sizeof(*D)); size_t *N = malloc(nf * sizeof(size_t)), *OFF = malloc(nf * sizeof(size_t));
    int *Wd = malloc(nf * sizeof(int)), *Hd = malloc(nf * sizeof(int)), *Sg = malloc(nf * sizeof(int));
    uint64_t total = 0;
    for (int f = 0; f < nf; f++) {
      D[f] = readf(argv[3 + f], &N[f]); if (!D[f]) { perror(argv[3 + f]); return 1; } total += N[f];
      OFF[f] = dicom_px(D[f], N[f], &Wd[f], &Hd[f], &Sg[f]); if (!OFF[f]) Wd[f] = Hd[f] = 0;
      const char *b = base(argv[3 + f]); put32(o, (uint32_t)strlen(b)); fwrite(b, 1, strlen(b), o);
      put32(o, (uint32_t)N[f]); put32(o, (uint32_t)OFF[f]); put32(o, Wd[f]); put32(o, Hd[f]); put32(o, Sg[f]);
    }
    int32_t *cur = 0, *prv = 0, *E = 0, *Ep = 0, *EW = 0, *EWp = 0; int PW = -1, PH = -1; const uint8_t *ph = 0; size_t phn = 0;
    for (int f = 0; f < nf; f++) {
      size_t hn = OFF[f] ? OFF[f] : N[f], tail = OFF[f] ? N[f] - OFF[f] - (size_t)Wd[f] * Hd[f] * 2 : 0;
      hdr_code(&a, 0, D[f], hn, ph, phn); ph = D[f]; phn = hn;
      if (!OFF[f]) continue;
      int W = Wd[f], H = Hd[f]; long px = (long)W * H;
      if (W != PW || H != PH) {   /* new size: start a new volume, nothing in front */
        free(cur); free(prv); free(E); free(Ep); free(EW); free(EWp);
        cur = calloc(px, 4); prv = calloc(px, 4); E = calloc(px * NP, 4); Ep = calloc(px * NP, 4); EW = calloc(px, 4); EWp = calloc(px, 4); PW = W; PH = H;
      }
      const uint8_t *s = D[f] + OFF[f]; for (long i = 0; i < px; i++) { int v = s[2 * i] | s[2 * i + 1] << 8; cur[i] = Sg[f] ? (int16_t)v : v; }
      slice(&a, m, 0, W, H, cur, prv, E, Ep, EW, EWp);
      int32_t *t; t = prv; prv = cur; cur = t; t = Ep; Ep = E; E = t; t = EWp; EWp = EW; EW = t;
      hdr_code(&a, 0, D[f] + N[f] - tail, tail, 0, 0);
    }
    enc_flush(&a); put32(o, (uint32_t)a.pos); fwrite(a.buf, 1, a.pos, o); long out = ftell(o); fclose(o);
    fprintf(stderr, "h3d: %d files, %llu -> %ld bytes, ratio %.3fx, %.2f s\n", nf, (unsigned long long)total, out, (double)total / out, (double)(clock() - t0) / CLOCKS_PER_SEC);
  } else {
    size_t n; uint8_t *in = readf(argv[2], &n); if (!in || n < 8 || memcmp(in, "H3D1", 4)) { fprintf(stderr, "not an h3d file\n"); return 1; }
    const uint8_t *p = in + 4; int nf = get32f(&p);
    char **name = malloc(nf * sizeof(char *)); size_t *N = malloc(nf * sizeof(size_t)), *OFF = malloc(nf * sizeof(size_t)); int *Wd = malloc(nf * sizeof(int)), *Hd = malloc(nf * sizeof(int)), *Sg = malloc(nf * sizeof(int));
    for (int f = 0; f < nf; f++) { uint32_t l = get32f(&p); name[f] = malloc(l + 1); memcpy(name[f], p, l); name[f][l] = 0; p += l;
      N[f] = get32f(&p); OFF[f] = get32f(&p); Wd[f] = get32f(&p); Hd[f] = get32f(&p); Sg[f] = get32f(&p); }
    uint32_t cl = get32f(&p); AC a; dec_init(&a, (uint8_t *)p, cl);
    int32_t *cur = 0, *prv = 0, *E = 0, *Ep = 0, *EW = 0, *EWp = 0; int PW = -1, PH = -1; uint8_t *ph = 0; size_t phn = 0; uint64_t total = 0;
    for (int f = 0; f < nf; f++) {
      uint8_t *d = malloc(N[f] + 1); size_t hn = OFF[f] ? OFF[f] : N[f];
      hdr_code(&a, 1, d, hn, ph, phn);
      if (OFF[f]) {
        int W = Wd[f], H = Hd[f]; long px = (long)W * H; size_t tail = N[f] - OFF[f] - (size_t)px * 2;
        if (W != PW || H != PH) { free(cur); free(prv); free(E); free(Ep); free(EW); free(EWp);
          cur = calloc(px, 4); prv = calloc(px, 4); E = calloc(px * NP, 4); Ep = calloc(px * NP, 4); EW = calloc(px, 4); EWp = calloc(px, 4); PW = W; PH = H; }
        slice(&a, m, 1, W, H, cur, prv, E, Ep, EW, EWp);
        uint8_t *s = d + OFF[f]; for (long i = 0; i < px; i++) { s[2 * i] = cur[i] & 255; s[2 * i + 1] = (cur[i] >> 8) & 255; }
        int32_t *t; t = prv; prv = cur; cur = t; t = Ep; Ep = E; E = t; t = EWp; EWp = EW; EW = t;
        hdr_code(&a, 1, d + N[f] - tail, tail, 0, 0);
      }
      char path[4096]; snprintf(path, sizeof path, "%s/%s", argv[3], name[f]); FILE *o = fopen(path, "wb"); if (!o) { perror(path); return 1; }
      fwrite(d, 1, N[f], o); fclose(o); total += N[f]; free(ph); ph = d; phn = hn;
    }
    fprintf(stderr, "h3d: %d files, %llu bytes restored, %.2f s\n", nf, (unsigned long long)total, (double)(clock() - t0) / CLOCKS_PER_SEC);
  }
  return 0;
}
