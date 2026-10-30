bzip2-hi encoder validation, 30 October 2026
bzc.c: test driver; build with the tuned sources: gcc -O2 -I../source -o bzc bzc.c ../source/{blocksort,huffman,
crctable,randtable,compress,decompress,bzlib}.c (needs a bz_version.h with #define BZ_VERSION "1.1.0-hi").
Results (tuned encoder, decoded by stock bzip2 1.0.8 / libbz2):
- 16 edge files at block sizes -1 and -9: empty, 1 byte, 1 MB of one byte, runs of 1-1000 (around bzip2's
  4-255 run-length limits), two symbols, random, text, an executable, sizes at the -1 and -9 block
  boundaries (99,981/99,982/100,001 and 899,981/899,982/900,001), 0/255 alternating, all 256 values:
  32 round trips, all exact.
- Fuzz: 300 random inputs (0 bytes to 300 KB; alphabets of 1-256; runs, patterns with noise) at random block
  sizes: all exact.
bz2_hi.py now checks its output with the stock libbz2 decoder before writing it.
