HX COMPETITION TEST 1.0
For scientific and peer review purposes only. Patent Pending (U.S. Provisional App. 64/192,843).
Copyright (c) 2026 Michael Hugh Harrington. This app's source: MIT License. HX itself: see source\HX\HX-LICENSE.txt.

WHAT IT DOES
Runs HX Fast and HX Complete, Michael Harrington's other tools, and 29 settings of published compressors (LZ4, Snappy,
LZAV, gzip/libdeflate, zstd, Brotli, bzip2, xz, 7-Zip LZMA2 and PPMd, BSC, bzip3, kanzi) on the same files, on your
computer. Every result is decoded and checked byte for byte, and every result is shown, the poor ones too.
Then it ranks them: an HX Fast race (everything about as fast to decode as HX Fast), an HX Complete race (everything
close to HX Complete's compression or better), and wins and losses in 10 speed tiers measured against LZ4.

HOW TO USE
1. Unzip and double-click HX-Competition-Test.exe.
2. Load a file, or tick "Run all files in a folder" and choose a folder.
3. Untick any program you want left out (the log lists what was left out). The ? button explains the races and tiers.
4. Runs: 1 to 10. Log: Basic, or Advanced (adds every command line and the SHA-256 of every program).
5. Run test. Cancel stops at once. Save log... writes the log (.txt) and a results table (.csv) beside it.
The log can be edited, notes added, and each new test is added below the last one.

FOR REVIEWERS (and their AI)
  METHOD.txt   every command line, the race and tier rules, and the known limits
  BUILD.txt    how every program was built, from which public release
  source\      this app (app), the Snappy/libdeflate front end (rivals), HX (HX), H-Deflate (H-Deflate)
  licenses\    the licenses of every program included
Michael Harrington's other tools (hs_zstd, rd-zstd, xz-hi, bzip2-hi, lzav-hi, HCM) are MIT; their source is in the
Harringtons_Insights repository on GitHub (CombaryMaker).
Self-test: HX-Competition-Test.exe /selftest FILE REPORT.txt runs every program once on FILE and writes REPORT.txt and
REPORT.txt.csv.
