# Random re-verification of wins — 28 October 2026

**Seed:** 2360932695 (drawn with Python `secrets`). Samples were drawn with `random.Random(seed).sample()` over each method's sorted list of recorded wins. **Rule:** 1 sample if a method has 1–2 wins, 2 if it has 3, otherwise 3. RAR was limited to its one win, sao, as instructed. Full lists: `registry_full.json`; draw: `draw.json`; raw measurements: `results.jsonl`.

**Measured for every sample:** compressed bytes, compression time, decompression time, CPU time and peak memory (CLI tools only), and an exact round trip. Compression is the median of 3 runs; decompression the median of 5–21. Where the result was within a few percent, the decode was re-timed in interleaved pairs (base, candidate, base, …), 15–31 pairs.

**Round trips:** every one of the 54 base and candidate measurements was exact. JPEG XL was checked pixel for pixel; PDF by page render.

## Summary

| Method | Sample | Sizes reproduced? | Win holds? | Compression time, base → ours |
|---|---|---|---|---|
| Record delta | kennedy zstd -22 | yes (26,399) | yes — decode 1.50 → 0.52 ms | 1.35 → 0.84 s (faster) |
| Record delta | kennedy Snappy | exact | yes — 1.21 → 0.86 ms | same |
| Record delta | x-ray zstd -19 | yes (4,806,136) | **no with the old inverse** (22.4 → 27.0 ms); **yes with a new prefix-sum inverse** (24.6 → 21.3 ms) | 5.1 → 7.8 s (slower) |
| Record delta (fast) | mr xz -0 | yes | tie within noise (5 runs: 3 faster, 2 slower) | 0.76 → 0.70 s |
| Activity map | kennedy gzip -9 | close (zlib 35,908 vs book 35,558) | yes — 2.48 → 1.60 ms | 0.54 → 0.02 s |
| xz-hi | hdfs.log | exact | yes | 0.16 → 0.13 s |
| xz-hi | openstack.log | exact | yes (interleaved 7.20 → 7.06 ms) | 0.63 → 0.47 s |
| lzma-hi | sao, x-ray, xml | liblzma, not the 7-Zip SDK | x-ray and xml yes; **sao not confirmed** (decode 3.6% slower) | about the same |
| 7z-hi | asyoulik, fields.c, kennedy | exact | yes, all 3 | kennedy 0.40 → 0.75 s |
| zstd-hi | counties.jsonl L22, plrabn12 L9, ptt5 L3 | yes; plrabn12 now bigger gain (−6.3%) | yes, all 3 | counties 72.9 → 36.3 s (faster); plrabn12 0.03 → 0.19 s |
| Brotli-hi | sum q8 | yes | yes | same |
| Brotli-hi | asyoulik q5, xargs q11 | yes | tie / slightly faster on interleaved re-time | same |
| bzip2-hi | plrabn12, sum | yes | yes | **0.07 → 22.9 s** (plrabn12), 0.01 → 3.5 s (sum) |
| bzip2-hi | xargs.1 | yes | tie (3.12 vs 3.15 ms) — not confirmed faster | 0.005 → 0.49 s |
| RAR | sao, -mcd- | exact | yes (interleaved 108.0 → 97.3 ms) | 1.9 → 3.8 s |
| PDF row filters | highlights, issue16316, tracemonkey_with_editable_annotations | yes | yes, all 3; pages identical | 0.24 → 2.0 s (Python rewrite) |
| HCM v3.3 fast | cp.html, fields.c, lcet10 | exact (6,437 / 2,522 / 89,345) | yes in size; Tier 4 speed as documented | 0.04–1.2 s |
| hcm-trit v2 | cp.html, ptt5, xml | yes | yes in size; decode +2 to +13% as documented | +10–18% |
| HCM-Bin v1 | kennedy max | exact (16,797) | yes in size; decode +36% vs v3.3 | 2.0 → 2.7 s |
| Harrington Exchange | kennedy lz4, zstd | exact (139,394 / 42,858) | yes in size; **first decoder written today confirms lossless** | lz4 1.43 → 0.19 s; zstd 1.73 → 2.33 s |
| Shrinking header | 256-entry tables | exact (1,793 bits = 225 bytes) | yes | under 0.4 ms |
| Column layout | mr ORC zlib, mr ORC zstd | exact | yes, both | faster (0.26 → 0.21 s; 0.17 → 0.11 s) |
| Column layout | sao Parquet zstd | yes | yes on interleaved re-time (136 → 130 ms) | 0.26 → 0.11 s |
| jxl-hi | RG3, x-ray | exact | yes (1.27× and 1.41× faster) | **0.65 → 1.95 s; 2.0 → 6.6 s** |
| jxl-hi | MR2 -P 5 -I 0 (BGS speed) | exact | yes — 4.2× faster decode for +3.6% | 0.44 → 0.09 s |
| JPEG-LS tuned | x-ray | yes (book units wrong, see below) | yes — 119.6 → 100.0 ms | 0.12 → 0.09 s |
| FLAC | george, jackson | yes | yes | 0.09 → 0.40 s |
| FLAC | theo | yes | **no** — interleaved 36.1 → 37.0 ms, faster in 7/21 | 0.05 → 2.5 s |
| WebP | 12 | yes | yes | **10.9 → 1.7 s (faster)** |
| WebP | 01, 10 | yes | tie within noise | 13.4 → 0.5 s; 11.3 → 0.3 s |

## Problems found

1. **Record delta, x-ray with zstd -19.** With the original inverse step, decoding was 4–15% slower in every run, so the Chapter 4 No-Pain claim did not reproduce. The original SIMD inverse advances only R bytes per 16-byte step, which is very slow at R = 2. A new in-register prefix-sum inverse for R = 1, 2, 4 and 8 fixes it: decoding is about 13% faster than plain zstd. **Book: Chapter 4 must name the inverse used.**
2. **JPEG2000_UNC award.** On interleaved re-timing, -P 5 -I 100 decoded slightly slower (21.7 → 23.8 ms, faster in 9 of 21). **Withdraw the No-Pain / Local award**; the size gain (16,643 → 15,684) is real.
3. **JPEG-LS units (Section 16.2).** "423,801 → 421,829 bytes" are really 1,000,000-bit-normalised figures. The actual sizes are 3,591,153 → 3,574,443 bytes.
4. **FLAC theo.** The size gain is real, but decoding was not faster on re-time. Count 4 FLAC wins, not 5.
5. **lzma-hi sao.** Not confirmed, but this run used liblzma's .lzma format because the 7-Zip SDK tool is no longer in the workspace. Treat as unconfirmed, not disproved.
6. **bzip2-hi xargs.1, WebP 01 and 10.** Decoding ties within noise. They are size wins with no decode loss, but not demonstrably faster.

## Compression time

Most tuned encoders are slower to compress, sometimes much slower:
- bzip2-hi: 0.07 → 22.9 s on plrabn12.txt;
- jxl-hi at -I 100: 3×;
- the PDF rewrite: 8×;
- RAR -mcd-: 2×;
- FLAC per-file search: 4–50×.

Several are faster to compress:
- record delta with zstd -22: 1.35 → 0.84 s;
- zstd-hi on the 27 MB JSONL: 72.9 → 36.3 s;
- WebP search settings: 6–40× faster than -z 9;
- ORC and Parquet with detected columns;
- the activity map with gzip.

Compression time is a one-off cost at write time. No-Pain judges the reader's side, so this does not change any verdict, but it is now recorded.
