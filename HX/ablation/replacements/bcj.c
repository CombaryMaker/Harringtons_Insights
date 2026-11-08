/* bcj.exe - x86 BCJ branch converter, the LZMA SDK / XZ / 7-Zip "x86" filter (Igor Pavlov, LZMA SDK Bra86.c, public domain;
   that file is compiled in unchanged). Replacement for the HX Ablation Test. Start position 0.
   Copyright (c) 2026 Michael Hugh Harrington. MIT License.   bcj f IN OUT   |   bcj i IN OUT */
#include "common.inc"
#include "Bra.h"
int main(int argc, char **argv) {
    if (argc < 4 || (argv[1][0] != 'f' && argv[1][0] != 'i')) { fprintf(stderr, "usage: bcj f IN OUT | bcj i IN OUT\n"); return 2; }
    uint64_t m; uint8_t *d = rd(argv[2], &m); Buf o = {0}; UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
    if (argv[1][0] == 'f') { header(&o, "BCJ1", m); size_t st = o.n; put(&o, d, (size_t)m); z7_BranchConvSt_X86_Enc(o.d + st, (SizeT)m, 0, &state); }
    else { uint64_t n; const uint8_t *p = check(d, m, "BCJ1", &n, 0); if (m != 13 + n) die("damaged file");
        o.d = (uint8_t *)malloc(n ? n : 1); o.n = n; memcpy(o.d, p, n); z7_BranchConvSt_X86_Dec(o.d, (SizeT)n, 0, &state); }
    wr(argv[3], o.d, o.n); return 0;
}
