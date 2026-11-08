/* grouping-map helpers: windows, GMP1 writer, integer log2 (no floating point) - MIT, Michael Hugh Harrington 2026 */
static uint32_t lg16(uint32_t x) {   /* log2(x) * 65536, integers only (repeated squaring), x >= 1 */
    int k = 31 - __builtin_clz(x); uint64_t y = (uint64_t)x << (31 - k); uint32_t r = (uint32_t)k << 16;
    for (int b = 15; b >= 0; b--) { y = (y * y) >> 31; if (y >= (1ull << 32)) { y >>= 1; r |= 1u << b; } } return r;
}
static void write_map(const char *f, uint32_t W, uint32_t C, const uint8_t *cls) {
    uint8_t K = 0; for (uint32_t i = 0; i < C; i++) if (cls[i] + 1 > K) K = (uint8_t)(cls[i] + 1);
    Buf o = {0}; put(&o, "GMP1", 4); put32(&o, W); put32(&o, C); put8(&o, K); if (C) put(&o, cls, C); wr(f, o.d, o.n);
}
static uint32_t window_arg(int argc, char **argv) { for (int i = 3; i + 1 < argc; i++) if (!strcmp(argv[i], "-b")) { long v = atol(argv[i + 1]); if (v < 64 || v > (1L << 30)) die("bad window size"); return (uint32_t)v; } return 65536; }
