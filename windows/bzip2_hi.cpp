// bzip2-hi: smaller standard .bz2 files (Harrington Insights).
// Uses the altered bzip2 encoder in bzip2-hi/source (it searches Huffman
// table settings per block) and tries every block size (-1 .. -9), keeping
// the smallest result. Any bzip2 decodes the output.
// bzip2/libbzip2 is copyright (C) 1996-2010 Julian R Seward (see COPYING);
// this front end: MIT License, Copyright (c) 2026 Michael Harrington.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "bzlib.h"

static const char *TOOL_NAME = "bzip2-hi", *TOOL_EXT = ".bz2", *TOOL_BASELINE = "the same encoder at -9";
static int tool_compress(const uint8_t *in, size_t n, uint8_t **out, size_t *outn, size_t *basen, char *note, size_t notesz) {
    if (n > 0x7FFFFFFFu) return -3;
    unsigned int cap = (unsigned int)(n + n / 100 + 600);
    uint8_t *best = NULL; unsigned int bn = 0; int bl = 0;
    uint8_t *tmp = (uint8_t *)malloc(cap);
    if (!tmp) return -1;
    for (int level = 9; level >= 1; level--) {
        unsigned int on = cap;
        if (BZ2_bzBuffToBuffCompress((char *)tmp, &on, (char *)in, (unsigned int)n, level, 0, 0) != BZ_OK) continue;
        if (level == 9) *basen = on;
        if (!best || on < bn) {
            if (!best) { best = (uint8_t *)malloc(cap); if (!best) { free(tmp); return -1; } }
            memcpy(best, tmp, on); bn = on; bl = level;
        }
    }
    free(tmp);
    if (!best) return -1;
    snprintf(note, notesz, "block size: -%d (%d00 KB blocks)", bl, bl);
    *out = best; *outn = bn;
    return 0;
}
static int tool_decompress(const uint8_t *in, size_t n, uint8_t **out, size_t *outn) {
    // grow the output buffer until it fits
    size_t cap = n * 8 + 4096;
    for (;;) {
        if (cap > 0xFFFFFFFFu) cap = 0xFFFFFFFFu;
        uint8_t *buf = (uint8_t *)malloc(cap);
        if (!buf) return -4;
        unsigned int on = (unsigned int)cap;
        int rc = BZ2_bzBuffToBuffDecompress((char *)buf, &on, (char *)in, (unsigned int)n, 0, 0);
        if (rc == BZ_OK) { *out = buf; *outn = on; return 0; }
        free(buf);
        if (rc != BZ_OUTBUFF_FULL || cap == 0xFFFFFFFFu) return -2;
        cap *= 4;
    }
}
#include "dropframe.h"
