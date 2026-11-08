/* blocks.exe - Plain fixed blocks: every window class 0 (the "no grouping" control). Copyright (c) 2026 Michael Hugh
   Harrington. MIT License.   blocks IN MAP [-b WINDOW] */
#include "common.inc"
#include "groupcommon.inc"
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: blocks IN MAP [-b WINDOW]\n"); return 2; }
    uint64_t n; uint8_t *d = rd(argv[1], &n); free(d); uint32_t W = window_arg(argc, argv); uint32_t C = (uint32_t)((n + W - 1) / W);
    uint8_t *cls = (uint8_t *)calloc(C ? C : 1, 1); write_map(argv[2], W, C, cls); return 0;
}
