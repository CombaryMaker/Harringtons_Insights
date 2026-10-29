# Harrington Insights

Compression improvements by **Michael Harrington**, built under two rules:

>1)  **The no-pain rule:** no loss of compression or decode time is allowed, only gains to either or both.
>2)  **The Big Gain rule:** A loss in one category can be offset by a sufficiently large enough gain in the other.

This repository has three tools:

| Tool | What it is | Output |
|---|---|---|
| **HCM v3.3** (`hcm/`) | Harrington Compression Method, v3 engine plus the Harrington Insights | `.hcm` |
| **bzip2-hi** (`bzip2-hi/`) | bzip2 with a smarter encoder | standard `.bz2` (any bzip2 decodes it) |
| **xz-hi** (`xz-hi/`) | xz with a per-file settings search | standard `.xz` (any xz decodes it) |

## The book and the research code

- **Book:** [`Harringtons-Insights.pdf`](Harringtons-Insights.pdf) — *Harrington's Insights: Improvements, Concepts, Strategies, Successes and Failures in Data Compression*.
- **Best results charts:** [`benchmarks/charts-2026-10-29/Harrington-Best-Results-Full.pdf`](benchmarks/charts-2026-10-29/Harrington-Best-Results-Full.pdf) — every tool built or modified here (originals, modifieds, mutants) against the stock tools on each standard benchmark; also in the book as Appendix G.
- **Research source code:** [`src/`](src/) — hcmx (HCM v3.3 + ternary pointer + HCM-Bin), the Harrington Exchange and its Blosc2 hybrid, the baseline comparisons and the record-delta benchmark. See [`src/README.md`](src/README.md).

## Harrington Sets: multi-core for single-core compressors (original method)

Harrington Sets (HCM v3.6, commands `s` fast / `t` max) lets compressors that have always run on one core use every core. The file is read in 64 KB windows measured on the spot; windows of the same kind are gathered from anywhere in the file into their own streams; random or already-compressed windows are excluded to a raw set through a layered exclusion map that keeps every window's exact position; each stream gets its own model and its own core. It works on any data with no format description. Loose ties only to equal-chunk parallel blocks, file grouping by extension, format-described stream splitting (OpenZL) and log clustering (Logzip), which all work differently. Results (2 cores): HCM on samba decodes 1.73x faster for +0.16% size; lpaq1-h 1.6-1.9x faster. Full write-up: [`docs/Harrington-Sets.txt`](docs/Harrington-Sets.txt); book Section 10.9.

## Results: Canterbury Corpus (all 11 files)

Sizes in bytes. Every file was decoded back and compared byte for byte.

| File | HCM v3 → **v3.3** | bzip2 -9 → **bzip2-hi** | xz -9e → **xz-hi** |
|---|---|---|---|
| alice29.txt | 37,342 → **36,636** | 43,202 → **43,139** | 48,528 → **48,472** |
| asyoulik.txt | 34,757 → **34,348** | 39,569 → **39,545** | 44,592 → **44,448** |
| cp.html | 6,451 → **6,437** | 7,624 → **7,585** | 7,652 → **7,616** |
| fields.c | 2,485 → 2,485 | 3,039 → **2,976** | 3,032 → **2,996** |
| grammar.lsp | 1,047 → 1,047 | 1,283 → **1,235** | 1,292 → **1,264** |
| kennedy.xls | 53,899 → **18,680** | 130,280 → **120,482** | 51,868 → **23,656** |
| lcet10.txt | 91,997 → **89,345** | 107,706 → **107,507** | 119,488 → **118,892** |
| plrabn12.txt | 129,146 → **125,047** | 145,577 → **145,289** | 165,456 → **165,276** |
| ptt5 | 45,083 → **28,125** | 49,759 → **49,599** | 39,860 → **39,052** |
| sum | 10,930 → **9,822** | 12,909 → **12,845** | 9,500 → **9,384** |
| xargs.1 | 1,464 → 1,464 | 1,762 → **1,697** | 1,812 → **1,776** |
| **All 11: 1,000,000 bits down to** | 147,504 → **125,743** | 193,081 → **189,235** | 175,424 → **164,663** |
| **Decode time, all 11 files** | 2.36 s → 2.35 s | 59.7 ms → **55.4 ms** | 32.1 ms → **24.8 ms** |

No file got bigger and no tool decodes slower. Decode times were measured on the same machine
(HCM: whole program, median of 9 runs, same compiler flags for both; bzip2 and xz: the stock
libbz2 / liblzma decoders run in-process, median of many runs). Only compression takes longer,
because the encoders try more options.

## The Harrington Insights

1. **Automatic record length.** A byte value that repeats at the same spacing three times in a row
   votes for that spacing; enough votes fix the record length (13 bytes for kennedy.xls, 216 for ptt5).
2. **Record contexts for binary files (HCM).** Once the record length is known, slots that only help
   text (the word contexts and the order-8 context) switch to "the bytes in the rows above".
   Same number of contexts, so the same work per byte.
3. **Line-position context for text (HCM).** In text files the order-6 slot becomes
   column-in-line + letters-so-far + class of the last byte.
4. **Word-position mixer weights (HCM).** The mixer picks its weights by position inside the word.
5. **Keep the better of two (HCM).** Each file is compressed with v3 and with v3.3; the smaller is
   kept, so no file is ever worse than v3. v3.3 still reads v3 and v2 files.
6. **Record length → xz delta filter (xz-hi).** The detected record length is offered to xz as a
   delta-filter distance; this more than halves kennedy.xls. Per file, xz-hi also searches
   lc/lp/pb, match effort, BCJ filters for executables, sizes the dictionary to the file and uses
   a CRC32 check.
7. **Search instead of rules of thumb (bzip2-hi).** For every block, the encoder tries 2-6 Huffman
   tables, 4/8/16/32 refinement passes, code-length limits 17 and 20 (both legal), and three
   starting splits, and writes the cheapest. Per file it also picks the best block size.

## Using it

**HCM (Windows):** drag a file onto `hcm/HCM.exe` (fast) or `hcm/HCM-Max.exe` (max). It writes
`NAME.hcm` next to the original after checking it decodes back. Drag a `.hcm` file on to restore it.

**Building HCM:** `clang++ -O2 -ffp-contract=off -o hcm hcm/hcm.cpp`
Command line: `hcm c IN OUT` (fast), `hcm x IN OUT` (max), `hcm d IN OUT` (restore).

**bzip2-hi:** build `bzip2-hi/source` (see its COMPILING.md, or
`gcc -O2 -DBZ_UNIX=1 blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c bzip2.c -o bzip2`
after copying `bz_version.h.in` to `bz_version.h`), then `bz2_hi.py INPUT`.
The only changed file is `compress.c`; see `compress.c.patch`.

**xz-hi:** `python3 xz-hi/xz_hi.py INPUT` (uses Python's built-in lzma module).

## Licenses

- HCM and the xz-hi / benchmark scripts: MIT, Copyright (c) 2026 Michael Harrington (see `LICENSE`).
- `bzip2-hi/source`: altered version of bzip2 by Julian Seward; bzip2's own license applies
  (`bzip2-hi/source/COPYING`). It is plainly marked as altered and is not the original bzip2.
- xz-hi uses liblzma from XZ Utils through Python; no XZ Utils code is included.
