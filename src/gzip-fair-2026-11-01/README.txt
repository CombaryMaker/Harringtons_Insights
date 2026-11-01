Fair gzip baselines (Andrew Goodwin's list), 2026-11-01, desktop Ryzen 5 7500X3D 6c/12t, 15 GB, NVMe, Windows 11.
File: silesia_4gb.bin (public, 4,026,833,020 bytes). Level 6 everywhere, 12 threads, 3 decode rounds (median), every
decode SHA-256 exact. Versions: pigz 2.8, bgzip (htslib) 1.21 (Windows build, see bgzip-windows-build), rapidgzip 0.16.1
(PyPI, run from a portable Python in the test folder), hs_wrap 1.5 with gzip -6 (zlib 1.3.1 build), --raw off, 16 MB blocks.

                                              1,000,000 bits   compress    decode median
gzip -6, one thread                                 321,924     116.1 s    14.16 s  (gzip)
  same .gz decoded by pigz -p 12                                            9.55 s
  same .gz decoded by rapidgzip -P 12                                       2.88 s
pigz -6 -p 12                                       321,970      13.1 s     9.77 s  (pigz)  /  2.71 s (rapidgzip)
bgzip -l 6 -@ 12                                    334,925      12.0 s     3.36 s  (bgzip)  /  7.27 s (rapidgzip)
hs_wrap 1.5 + gzip -6, Harrington Sets              322,017   see below     4.02 s
hs_wrap 1.5 + gzip -6, plain blocks (--sets 0)      321,971   see below     4.03 s  (one round 15.2 s, outlier)

What this shows, plainly:
- rapidgzip, decoding an ordinary single-stream .gz file on 12 threads, is the fastest gzip decoder here (2.7-2.9 s),
  needs no special format, and beats hs_wrap + gzip (4.0 s) and bgzip (3.4 s). For gzip, the decode-speed claim of
  hs_wrap does not hold against this baseline. (The 3.25x hs_wrap result from 31 Oct was against single-thread gzip.)
- bgzip decodes fast in parallel but its 64 KB blocks cost 4% in size (334,925 vs ~322,000).
- hs_wrap 1.5 is smaller than bgzip and within 0.03% of plain gzip; Sets and plain blocks are equal on Silesia, as with
  zstd - the Sets gain shows on mixed data, not here.
- hs_wrap's compression times were lost to a harness bug (its status line was captured into the timer) and were re-timed
  separately; see compress-retime.txt.

hs_wrap 1.5 compress, re-timed: Sets 24.9 s, plain blocks 24.5 s (see src/hcm-v36/memory-2026-11-01/README.txt).
