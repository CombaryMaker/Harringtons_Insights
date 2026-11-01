4 GB race, 2026-11-01, Michael Harrington's desktop (Ryzen 5 7500X3D 6c/12t, 15 GB RAM, NVMe, Windows 11), zstd 1.5.6 level 3,
12 jobs, 5 decode rounds, hs_zstd decodes with --flush (output forced to disk before the clock stops), SHA-256 round trips.
Test data is public only, rebuilt by prep_4gb.ps1 (download URLs and SHA-256 of every archive in sources-and-hashes.txt):
  silesia_4gb.bin  12 Silesia files joined and repeated 19 times, 4,026,833,020 bytes
  mixed_4gb.tar    tar of Linux 6.6 source + Blender 4.2.3 Windows portable + pdf.js repo (JS + ~1,000 PDFs) + NYT covid-19-data
                   CSV, 3,179,306,496 bytes (Windows cannot create the kernel's symlinks, so those entries are absent)
hs_zstd 1.3 = Sets with the raw rule off and safe smoothing (defaults); "plain blocks --sets 0" = the same program with no
grouping = the fair baseline for what Sets add; "1.2 rule" = the old entropy raw rule. Memory: "private commit" is the
program's own memory; "working set" also counts the memory-mapped output file as it is written (not allocation).

Silesia 4 GB, 1,000,000 bits down to / compress / decode median (private memory in results file):
  zstd one thread 314,102 / 14.71 s / 3.73 s;  zstd -T12 same size / 2.93 s / 4.25 s;  zstd --long=27 -T12 311,386 / 4.76 / 3.79
  pzstd -p12 315,013 / 4.09 / 2.85;  hs_zstd 1.3 16 MB 314,315 / 3.50 / 2.51;  plain blocks 16 MB 314,165 / 3.24 / 2.39
  hs_zstd 1.3 4 MB 315,391 / 2.94 / 2.27;  plain blocks 4 MB 315,230 / 3.07 / 2.20
  -> block splitting + direct decode beats pzstd 1.25-1.30x and zstd 1.6x; Sets add nothing on Silesia (plain blocks 0.1 s faster).
Mixed 3.2 GB tar:
  zstd one thread 244,965 / 7.90 / 2.23;  zstd -T12 same / 2.17 / 2.19;  zstd --long=27 -T12 233,113 / 3.26 / 2.20-8.62 s (unstable)
  pzstd -p12 246,879 / 2.83 / 1.92;  hs_zstd 1.3 16 MB 244,338 / 2.16 / 1.75 (301 MB);  plain blocks 16 MB 245,319 / 2.14 / 1.67 (241 MB)
  1.2 rule 16 MB 246,163 / 2.21 / 1.72;  hs_zstd 1.3 4 MB 247,207 / 1.90 / 1.75 (129 MB);  plain blocks 4 MB 247,601 / 1.97 / 1.78 (101 MB)
  -> on real mixed data Sets are smaller than plain blocks at both block sizes, smaller than single-stream zstd and pzstd, and
     decode 1.27x faster than zstd and 1.10x faster than pzstd; plain blocks still decode 0.08 s faster at 16 MB.
     zstd --long is 4.6% smaller than everything (long-range repeats inside the kernel tree and Blender) but its decode timings
     swung from 2.2 to 8.6 s across rounds.
