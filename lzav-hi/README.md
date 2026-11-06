# lzav-hi: a stronger encoder for the unchanged LZAV format

Michael Hugh Harrington, 2026. MIT License. Built on [LZAV](https://github.com/avaneev/lzav) by Aleksey Vaneev
(MIT), whose `lzav.h` is included **unmodified** (byte-identical to upstream commit ff1a1419, v7.1).

## What it is

`lzav_hi.h` is a new *encoder* for LZAV's own stream format (format 4). It writes every block with LZAV's own
`lzav_write_blk_4()` / `lzav_write_fin_4()`; only the choice of blocks differs. The output is decoded by the
**stock, unmodified `lzav_decompress()`**. Same approach as Michael's xz-hi, bzip2-hi and lzma-hi.

Method (classic public LZ77 techniques, nothing format-changing):
- binary-tree match finder over the whole input (search depth 48, nice length 512);
- optimal (shortest-path) parsing priced with LZAV's exact block sizes; the DP state includes LZAV's
  offset-carry shift (`csh`), so the cost and legality of every reference block are exact;
- minimum reference length 4 in the prefix byte (the stock decoder accepts it; stock compressors use 5/6).
  `-m5` gives mref 5, the value stock HI writes, for about 1% less gain.

## Results: Silesia (12 files, per file, summed)

Cloud VM, Intel Xeon @ 2.10 GHz, 2 vCPU, gcc 13.3 -O3, single thread, in-memory, median of 3-5 runs, every
output decoded and checked byte for byte. Stock rows reproduce Vaneev's published ratios exactly (39.23% / 34.55%).

| Encoder | Compressed bytes | Ratio | % of original | Compress MB/s | Decompress MB/s (stock decoder) |
|---|---:|---:|---:|---:|---:|
| LZAV 7.1 default | 83,153,299 | 2.549x | 39.23% | 347 | 1,941 |
| LZAV 7.1 HI | 73,228,616 | 2.894x | 34.55% | 48.8 | 1,640 |
| **lzav-hi (this)** | **61,746,209** | **3.432x** | **29.13%** | **1.40** | **1,404** |

- lzav-hi output is **15.7% smaller than LZAV HI** (19.4% higher ratio), and decodes with the stock decoder.
- **Costs, plainly:** compression is about 35x slower than HI (xz -9 class: ~1.4 MB/s on this VM), and
  decompression is **about 14% slower** than HI on Silesia overall (more, shorter matches). Text and tables
  (dickens, nci, samba, webster, xml) decode at about HI speed; binaries (x-ray, sao, mozilla, osdb) decode
  slower (x-ray 602 vs 1,211 MB/s). So "same format, smaller files", but **not** "same decode speed".
- Per-file numbers, LZ4 / LZ4HC rows (measured on 8 of the 12 files) and the parameter grid are in RESULTS.txt.

## hs_lzav (Harrington's Sets + parallel blocks)

`hs_lzav.cpp` splits one buffer into independent standard LZAV streams (4 MB blocks), optionally grouped by
Harrington's Sets, and decodes them on all cores straight into the output. On this 2-vCPU VM (silesia
concatenated, 212 MB): 1 thread 2,006 MB/s, 2 threads 2,198 MB/s (+10% only; the VM appears memory-bound);
blocking costs 1.4% in size (2.505x vs 2.541x whole-file). Sets did not help: +0.2% smaller on Silesia, 0.4%
larger on a 1 MB-interleaved mixed file, and decoding non-contiguous blocks costs an extra copy. Not a result to
pitch from this machine; it needs a real multi-core test.

## Build and verify

```
./build.sh verify     # builds bin/bench, bin/lzavhi, bin/hs_lzav; clones upstream LZAV, shows lzav.h is
                      # identical, builds bin/dec_upstream against UPSTREAM lzav.h only
bin/lzavhi c FILE FILE.lzavhi          # 8-byte length + raw LZAV stream
bin/dec_upstream FILE.lzavhi FILE.out  # stock upstream decoder
cmp FILE FILE.out
bin/bench -r 5 -c lzav,lzavhi,hi2 silesia/*
```
Silesia was taken from github.com/DataCompression/corpus-collection (sha256 of each file in RESULTS.txt).

Files: `lzav_hi.h` (encoder), `lzav.h` (stock), `bench.c`, `lzavhi_cli.c`, `hs_lzav.cpp`, `make_mixed.py`,
`build.sh`, `NOTES.txt`, `RESULTS.txt`, `LICENSE.txt`, `LZAV-LICENSE.txt`.
