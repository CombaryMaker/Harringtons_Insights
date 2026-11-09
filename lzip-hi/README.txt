lzip-hi and hs_lzip - 8 November 2026 (asked by Tansy; Michael Hugh Harrington, MIT)
Built on lzlib 1.16 (Antonio Diaz Diaz, 2-clause BSD), unmodified. Compared with lzip 1.26 and plzip 1.13 (stock).
Test machine: cloud Linux, 2 cores. All round trips exact.

lzip-hi (lzip_hi.c): standard .lz, decoded by stock lzip. Dictionary covers the whole file (max 512 MiB) + match length
limit chosen on a sample. Silesia 12 files: lzip -9 48,335,267 -> 48,315,924 bytes (-0.04%), compress 182 -> 1,032 s,
decode 3.17 -> 3.18 s. Canterbury 11 files: 491,667 -> 490,253 (-0.29%). Best file kennedy.xls -2.2%.
Verdict: small size gain, ~5.7x compress time, same decode speed.

hs_lzip (hs_lzip.cpp): parallel blocks as lzip members; --sets 0 = standard multi-member .lz (stock lzip and plzip read it);
--sets 3 = Harrington's Sets (HSL1 container). silesia.tar (211,957,760 bytes), level 6, 2 threads:
  lzip -6 (1 thread)            49,789,902   comp 108.5 s   decode 3.05 s
  plzip -6 (16 MB data size)    50,192,914   comp  50.6 s   decode 1.84 s
  hs_lzip plain 16 MB           50,353,024   comp  51.9 s   decode 2.08 s
  hs_lzip Sets 16 MB            50,354,571   comp  54.3 s   decode 2.20 s
  hs_lzip Sets 4 MB             51,499,804   comp  40.1 s   decode 2.01 s
  plzip -6 -B 4MiB              51,467,360   comp  38.1 s   decode 1.65 s
Verdict: plzip wins on this test - smaller and faster at both block sizes; Sets added nothing on silesia.tar
(same result as hs_zstd on Silesia). Not measured on 12 cores.

FILES
  lzip_hi.c        lzip-hi source (C)      lzip-hi.exe   Windows x64 build
  hs_lzip.cpp      hs_lzip source (C++17)  hs_lzip.exe   Windows x64 build
  lzip-hi-vs-lzip9-results.txt   per file: name | original | lzip -9 bytes, compress s, decode s, check | lzip-hi same
  hs_lzip-vs-plzip-silesia-tar-results.txt
  lzlib-COPYING.txt              lzlib license (lzlib is linked into both programs, unmodified)

USAGE
  lzip-hi c IN OUT   compress (search)     lzip-hi s IN OUT   stock lzip -9 settings     lzip-hi d IN OUT   decompress
  hs_lzip c IN OUT [--level 0-9] [--block-mb B] [--jobs N] [--sets 0|1|2|3]      hs_lzip d IN OUT [--jobs N]

BUILD
  Both link lzlib 1.16 (download.savannah.gnu.org/releases/lzip/lzlib/), unmodified.
  Windows builds: llvm-mingw 20240619 (x86_64), -O2, static.
  Linux:  gcc -O2 lzip_hi.c lzlib.c -o lzip-hi        g++ -O2 -std=c++17 hs_lzip.cpp lzlib.o -lpthread -o hs_lzip

License: MIT (c) 2026 Michael Hugh Harrington. lzlib: 2-clause BSD (Antonio Diaz Diaz), see lzlib-COPYING.txt.
