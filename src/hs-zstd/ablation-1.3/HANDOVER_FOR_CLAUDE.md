# Handover for a Claude session: hs_zstd 1.3-ablation

Paste or attach this file at the start of a Claude session so it has the full context.

## Background

- hs_zstd 1.2 is Michael Harrington's compressor: it labels a file in 64 KB windows by byte statistics, stores
  high-entropy windows raw, groups the rest into at most three "Harrington Sets" with k-means, cuts each set into
  blocks, compresses every block as an independent zstd frame, and decodes all blocks in parallel straight into a
  memory-mapped output.
- An independent reviewer added test options to measure what the Sets contribute. This package is that version,
  1.3-ablation. The `.hsz` format is unchanged: every file it makes decodes with 1.2, and with no new options it
  behaves exactly like 1.2.

## What changed (all marked `1.3` in hs_zstd.cpp)

- `--sets N` (0–3, default 3): the most sets k-means may form. `--sets 0` keeps the file in its original order as one
  set, which is plain block splitting and the fair baseline for measuring Sets. It also turns the raw rule off unless
  `--raw` is given.
- `--raw entropy|test|off` (default `entropy`, the 1.2 rule): `test` runs a zstd level-1 trial over the file with a
  2 MB window (in pass 1, per thread, with up to 2 MB of earlier data as a prefix) and stores a window raw only if the
  trial cannot shrink it by 0.1%; `off` stores nothing raw.
- `--smooth 1.2|safe` (default `1.2`): `safe` stops run smoothing from turning compressible runs into RAW.
- `--flush` (decompress only): msync/fsync on Linux, FlushViewOfFile/FlushFileBuffers on Windows, before timing stops.
- Removed an unused helper (`copy_n`) and a no-op line; no other behaviour changed.

## Findings so far (sizes only, 16 MB blocks, one cloud machine; all round trips exact)

| File | Sets as published | `--sets 0` | Sets `--raw off` | Sets `--raw test` | zstd -3 --long=27 |
| --- | --- | --- | --- | --- | --- |
| switch_1mb.bin (24 MB) | 16,063,625 | 14,807,670 | 13,478,425 | 13,612,813 | 14,737,134 |
| dup_random_chunks.bin (48 MB) | 50,331,685 | 38,512,680 | 36,842,315 | 50,331,685 | 21,736,643 |
| Silesia, 12 files joined (212 MB) | 66,615,351 | 66,614,016 | 66,679,523 | 66,664,472 | not run |

1. Sets help (−9% against `--sets 0`) when different kinds of data alternate in stretches of about 1 MB, and zstd
   `--long` does not achieve this. This is the case worth developing.
2. On ordinary files Sets are neutral (Silesia: 0.002% difference).
3. The entropy raw rule is the main problem: windows whose bytes look random are stored raw even when they repeat
   earlier data. Storing the source of a repeat raw also removes it from the compressed stream, so later copies
   compress worse. `--raw off` beats the 1.2 rule on every file tested.
4. Smoothing issue in 1.2: `labels()` relabels runs shorter than 8 windows with the previous run's label. When k-means
   splits near-identical windows into several clusters, there are many short runs; the first short run after raw
   windows becomes RAW and the next copies it, so whole compressible stretches end up raw. In dup_random_chunks.bin
   every window, text included, is stored raw. `--smooth safe` prevents this.
5. For duplicated content far apart, zstd `--long` is far better than any block-split variant.

## Next steps

1. Build: `./build.sh` (Linux, WSL2, macOS or MSYS2/MinGW-w64), or link zstd's static library as for 1.2.
2. Make the stress files: `python make_testfiles.py testfiles` (needs numpy).
3. Run: `python run_ablation.py --exe ./hs_zstd --jobs 12 --rounds 5 --blocks 4,16 FILES...` with at least: joined
   Silesia, enwik8 or enwik9, both stress files, and one or two real archives with interleaved file types.
4. For timings: exclude the test folder from Windows Defender, close other programs, use five or more rounds, and keep
   `--flush` on (the runner does this).
5. Report every result against `--sets 0` at the same block size, alongside pzstd, single-thread zstd and zstd `--long`.
6. Possible improvements to test: make `--raw off` the default; keep safe smoothing; merge clusters whose centres are
   close so homogeneous data stays in one set; choose the number of sets by how well clustering separates windows;
   compare with 7-Zip-style grouping by file type.

## Things to keep in mind

- Do not change the `.hsz` format while testing, so results stay comparable and files stay decodable by 1.2.
- Avoid repeated copies of one corpus as test data: their long-range repeats distort both sizes and speeds.
- Sizes are deterministic and comparable across machines; speeds are only comparable within one machine.
