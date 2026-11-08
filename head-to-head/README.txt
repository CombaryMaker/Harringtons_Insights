HARRINGTON HEAD-TO-HEAD 1.0
Michael Hugh Harrington - 7 November 2026 - MIT License (the app and Michael's tools; other programs keep their own licenses)

WHAT IT DOES
Puts one of Michael Harrington's open-source compression tools against a fixed, same-tier opponent on the same file, on
your own computer, and shows compressed size, ratio, compression time, decompression time and peak memory side by side.
Every result is decompressed and compared byte for byte with the original before it counts.

HOW TO USE
1. Unzip the folder anywhere and double-click Harrington-Head-to-Head.exe.
2. Pick a Harrington tool on the left. The opponent on the right is fixed for that tool.
3. Pick a test file from the list (the data types that tool was designed for), or press "Your own file...".
   Listed files come from public corpora. If one is missing, "Where to get it" opens its download page and the
   testfiles folder; put the file there. The app shows each file's size and SHA-256 so anyone can confirm it.
4. Choose Repeats (median of 1, 3, 5 or 7 runs) and Run first (alternate every repeat, Harrington tool first, or
   opponent first), then press "Run head-to-head".
5. "Copy results" puts a plain-text report on the clipboard, ready to post.

FAIRNESS RULES (built into the program, not in an editable file)
- Matchups, levels and thread counts are fixed. Tools cannot be paired freely, so a slow, strong compressor is never
  put against an ultra-fast one.
- Both tools run on the same file, one after the other, never at the same time.
- Compression and decompression are timed separately (wall clock, including program start) and the median is shown.
- You choose which side runs first; the default alternates every repeat.
- These test files are the data types each Harrington tool was designed for. Each matchup also has a note saying where
  the opponent is broader or where the Harrington tool is slower.

THE FIXED MATCHUPS
  Harrington tool                          Opponent(s)                          Note
  hs_zstd 1.3 (level 3, all cores)         zstd 1.5.6 -3, same thread count     built for large files (64 MB+)
  HCM v3.6.1 (fast)                        ZPAQ 7.15 -m5; lpaq1 -6              context-mixing tier
  zpaq-hi -m5                              ZPAQ 7.15 -m5                        same program and level
  lpaq1-h -6 (best of 6 settings)          lpaq1 -6                             same engine and memory
  bzip2-hi                                 bzip2 -9                             both standard .bz2
  lzav-hi                                  LZAV 7.1 HI encoder                  same format, stock decoder
  xz-hi                                    xz 5.6.3 -9e                         both standard .xz
  rd-zstd (record delta + zstd -19)        zstd 1.5.6 -19                       for fixed-record data

TEST FILES (public)
  Canterbury corpus: alice29.txt, lcet10.txt, plrabn12.txt, kennedy.xls, ptt5, sum
      https://corpus.canterbury.ac.nz/descriptions/#cantrbry
  Silesia corpus: dickens, mozilla, samba, x-ray, mr, ooffice, nci
      https://sun.aei.polsl.pl//~sdeor/index.php?page=silesia
  enwik8 (Large Text Compression Benchmark): https://mattmahoney.net/dc/textdata.html

PROGRAMS IN THE tools FOLDER (all built from public source; see source\ and BUILD.txt)
  hs_zstd.exe, HCM.exe, rd-zstd.exe, xz-hi.exe, bzip2-hi.exe, lzav-hi.exe, zpaq-hi.exe, lpaq1-h-best.exe - Michael's tools (MIT)
  lpaq1-h.exe - lpaq1-h (GPL v3, built on Matt Mahoney's lpaq1; source included)
  zstd.exe - zstd 1.5.6 (BSD, Meta)                 xz.exe - XZ Utils 5.6.3 (0BSD; see licenses\xz-COPYING.txt)
  bzip2-stock.exe - libbz2 1.1 (BSD-style, Julian Seward); byte-identical to bzip2 -9 output
  zpaq-stock.exe - libzpaq 7.15 (public domain, Matt Mahoney), single-stream front end
  lpaq1.exe - lpaq1 (GPL, Matt Mahoney; one 64-bit Windows pointer fix, source included)
  LZAV 7.1 (MIT, Aleksey Vaneev) is inside lzav-hi.exe unmodified; its "s" mode is the stock encoder.

NOTES
- Peak memory is the most memory the program (and any helper it starts) committed during the run.
- Times include program start-up for both sides equally. Close other heavy programs before testing.
- Temporary files go to your TEMP folder (harrington-h2h) and are deleted after each run.
- Self-test: Harrington-Head-to-Head.exe /selftest FILE REPORT.txt runs every matchup on FILE and writes a report.

Repository: https://github.com/CombaryMaker/Harringtons_Insights
