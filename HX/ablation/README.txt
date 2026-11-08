HX ABLATION TEST 1.0
For scientific and peer review purposes only. Patent Pending (U.S. Provisional App. 64/192,843).
Copyright (c) 2026 Michael Hugh Harrington. Research and peer review use allowed; see LICENSE.txt (same license as HX).

WHAT IT DOES
Runs HX with each of Michael Harrington's stages either On, or Off and replaced by a published, well-known alternative,
so anyone can measure what each stage adds. Every result is decoded with the normal HX decoder and checked byte for
byte (SHA-256). Every number in the log is measured on your computer; nothing is estimated.

HOW TO USE
1. Unzip and double-click HX-Ablation-Test.exe.
2. Choose HX Fast or HX Complete.
3. For each stage choose On (Michael's stage) or Off. When Off, pick the replacement in the box next to it.
   The ? next to a stage name explains the stage in plain language; the ? at the right explains the option shown.
   "Enable all" / "Disable all" switch every stage at once.
4. Load a file, or tick "Run all files in a folder independently" and choose a folder (each file is tested on its own,
   with totals at the end).
5. Runs: 1 to 10 (default 5). Log: Basic, or Advanced (adds every step and HX's own step-by-step report).
6. Run test. Cancel stops at once and removes the temporary files. Save log... writes the log as a .txt file.

THE STAGES IN THIS BUILD
  Decide: H Sampler             Off -> Try every line in full | Sample picks the winner (BtrBlocks style)
  Decide: H-Direct router       not in this public build (the router's training table is not included) - same in every run
  Group: Harrington's Sets      Off -> Plain fixed blocks | File-type grouping* | Byte-histogram clustering*
  Order: H Processor            not in this public build - same in every run
  Transform: Exchange lines     Off -> None (plain entropy coding) | Byte shuffle* | Bitshuffle* | Delta* | x86 BCJ* | Dictionary*
  Leftovers: Cascade repeat line Off -> Off | Block deduplication (FastCDC)*
  Route: Split route            Off -> Whole-file best-of
  * outside replacement programs, included in the tools folder (MIT, source in HX/ablation/replacements on GitHub).
    Each follows REPLACEMENT-SPEC.txt and its published source; each was checked for exact round trips and identical
    output on repeated runs.
  The standard coders inside HX (Huff0, FSE, rANS, zstd, and in HX Complete also BSC, bzip3, LZMA, PPMd, Brotli) are the
  same in every run.

WHAT IS MEASURED
  Size as "100% -> x%" and in KB/MB/GB (1 MB = 1,000,000 bytes), exact byte counts, compression ratio, bits per byte,
  compress and decompress time (median, min, max of the runs, wall clock including program start-up), MB/s, CPU time,
  peak committed memory, the computer's CPU, cores, RAM and Windows version, and the SHA-256 of hx.exe and every
  replacement program used. Energy use is not reported because Windows cannot measure it per program.

FILES
  HX-Ablation-Test.exe   the app          tools\hx.exe   HX with the ablation switches (encoder only; its default output is
  byte-identical to the released HX, and every file it writes decodes with the normal "hx d")
  Self-test: HX-Ablation-Test.exe /selftest FILE REPORT.txt [Fast|Complete] runs all-on, each replacement alone, and all-off.

Repository: https://github.com/CombaryMaker/Harringtons_Insights/tree/main/HX/ablation
