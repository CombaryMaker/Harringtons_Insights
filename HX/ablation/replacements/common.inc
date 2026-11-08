/* shared helpers (copied into each program at build time by #include; MIT, Michael Hugh Harrington 2026) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
static int mv(const char *a, const char *b) { return MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : -1; }
#else
static int mv(const char *a, const char *b) { return rename(a, b); }
#endif
static void die(const char *m) { fprintf(stderr, "%s\n", m); exit(1); }
static uint8_t *rd(const char *f, uint64_t *n) {
    FILE *p = fopen(f, "rb"); if (!p) die("cannot open input");
    _fseeki64(p, 0, SEEK_END); int64_t s = _ftelli64(p); _fseeki64(p, 0, SEEK_SET); if (s < 0) die("cannot size input");
    uint8_t *b = (uint8_t *)malloc(s ? (size_t)s : 1); if (!b) die("out of memory");
    if (s && fread(b, 1, (size_t)s, p) != (size_t)s) die("read error"); fclose(p); *n = (uint64_t)s; return b;
}
typedef struct { uint8_t *d; size_t n, cap; } Buf;
static void put(Buf *b, const void *p, size_t m) { if (b->n + m > b->cap) { b->cap = (b->n + m) * 2 + 1024; b->d = (uint8_t *)realloc(b->d, b->cap); if (!b->d) die("out of memory"); } memcpy(b->d + b->n, p, m); b->n += m; }
static void put8(Buf *b, uint8_t v) { put(b, &v, 1); }
static void put16(Buf *b, uint16_t v) { uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; put(b, t, 2); }
static void put32(Buf *b, uint32_t v) { uint8_t t[4]; for (int i = 0; i < 4; i++) t[i] = (uint8_t)(v >> 8 * i); put(b, t, 4); }
static void put64(Buf *b, uint64_t v) { uint8_t t[8]; for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> 8 * i); put(b, t, 8); }
static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t g64(const uint8_t *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static void wr(const char *f, const uint8_t *d, size_t n) {   /* write to OUT.tmp, then rename: never a partial OUT */
    size_t L = strlen(f); char *t = (char *)malloc(L + 5); memcpy(t, f, L); memcpy(t + L, ".tmp", 5);
    FILE *p = fopen(t, "wb"); if (!p) die("cannot create output"); if (n && fwrite(d, 1, n, p) != n) { fclose(p); remove(t); die("write error"); }
    if (fclose(p)) { remove(t); die("write error"); } if (mv(t, f)) { remove(t); die("cannot rename output"); } free(t);
}
static void header(Buf *b, const char *magic, uint64_t n) { put(b, magic, 4); put8(b, 1); put64(b, n); }
static const uint8_t *check(const uint8_t *d, uint64_t m, const char *magic, uint64_t *n, size_t need) {
    if (m < 13 + need || memcmp(d, magic, 4) || d[4] != 1) die("not a file made by this program"); *n = g64(d + 5); return d + 13;
}
static int argval(int argc, char **argv, const char *flag, int def) { for (int i = 4; i + 1 < argc; i++) if (!strcmp(argv[i], flag)) return atoi(argv[i + 1]); return def; }
