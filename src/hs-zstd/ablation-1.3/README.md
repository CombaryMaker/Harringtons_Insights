# hs_zstd 1.3-ablation: testing what Harrington Sets contribute

This is hs_zstd 1.2 with three options added for testing. Nothing else changes: the `.hsz` format is the same, every
file made by this version decodes with 1.2, and with no new options it behaves exactly like 1.2.

| Option | Values | What it does |
| --- | --- | --- |
| `--sets N` | 0–3 (default 3) | The most sets k-means may form. `--sets 0` keeps the file in its original order as one set: plain block splitting, the baseline for measuring Sets. It also switches the raw rule off unless `--raw` is given. |
| `--raw MODE` | `entropy` (default), `test`, `off` | Which windows are stored uncompressed. `entropy` is the 1.2 rule (above 7.99 bits/byte). `test` runs a fast zstd level-1 trial over the file with a 2 MB window and stores a window raw only if the trial cannot shrink it at all. `off` stores nothing raw; zstd already stores incompressible data raw inside its frames. |
| `--smooth M` | `1.2` (default), `safe` | Run smoothing. `safe` never turns a compressible run into raw windows (see "A smoothing issue" below). |
| `--flush` | decompress only | Forces the output to disk before the clock stops, so timings include writing it out rather than only filling memory. |

All changes are marked `1.3` in the source.

## Build

    ./build.sh                       # Linux, WSL2, macOS or MSYS2/MinGW-w64; fetches and builds zstd 1.5.6

or, with zstd's library already built: `g++ -O2 -std=c++17 -pthread hs_zstd.cpp -I<zstd>/lib <zstd>/lib/libzstd.a -o hs_zstd`.
With MSVC, compile `hs_zstd.cpp` as C++17 and link zstd's static library as before.

## Running the comparison

    python make_testfiles.py testfiles                     # two seeded stress files (needs numpy)
    python run_ablation.py --exe ./hs_zstd --jobs 12 --rounds 5 --blocks 4,16 FILE [FILE ...]

`run_ablation.py` compresses each file with seven variants (Sets as published; `--sets 0`; `--raw test`; `--raw off`;
`--sets 0 --raw test`; `--smooth safe`; `--smooth safe --raw off`) at each block size, plus single-thread zstd and pzstd if
they are on PATH. It decodes every result several times with `--flush`, checks each round trip by SHA-256, and writes
`ablation_results.csv` with size, compress time, every decode time, the median, MB/s and peak memory (if `psutil` is
installed). It ends with each variant's size relative to `--sets 0` at the same block size.

Suggested files: all 12 Silesia files joined into one (the most realistic single test), enwik8 or enwik9, the two stress
files, and one or two real archives where file types are interleaved, such as a tar of a source tree with images, or a
folder of mixed documents. Repeated copies of one corpus are best avoided: they are full of long-range repeats that
zstd level 3 cannot see, which distorts both sizes and speeds.

For timing, close other programs, exclude the test folder from Windows Defender (a 0.5 s webster decode against
0.05 s for other files is typical of antivirus scanning new files), and use at least five rounds.

## Results so far

Sizes in bytes, 16 MB blocks, one cloud machine (sizes only; speeds need a multi-core machine). Every round trip was exact.

| File | Sets as published (1.2) | `--sets 0` | Sets, `--raw off` | Sets, `--raw test` | `zstd -3 --long=27` |
| --- | --- | --- | --- | --- | --- |
| switch_1mb.bin (stress file, 24 MB) | 16,063,625 | 14,807,670 | **13,478,425** | 13,612,813 | 14,737,134 |
| dup_random_chunks.bin (stress file, 48 MB) | 50,331,685 | 38,512,680 | 36,842,315 | 50,331,685 | **21,736,643** |
| Silesia, all 12 files joined (212 MB) | 66,615,351 | **66,614,016** | 66,679,523 | 66,664,472 | — |
| Mixed archive of 18 synthetic files (18 MB) | 11,741,939 | **10,892,160** | 10,941,189 | 10,976,517 | — |
| Type switches every 1 MB, 7 types (24 MB) | 18,637,079 | 14,421,638 | **14,389,387** | 14,417,221 | — |

What this shows:

1. **Sets can genuinely help, when data of different kinds is interleaved in stretches of about a megabyte.** On
   switch_1mb.bin, grouping cuts the size by 9% against plain block splitting, and zstd's long-range mode gets only
   0.5%. Grouping puts similar content together inside each block, so zstd's statistics and recent matches fit better.
   This is the case worth pursuing, and it is also what archivers such as 7-Zip do by grouping files by type.
2. **On ordinary files, Sets are neutral.** On joined Silesia, Sets and `--sets 0` differ by 0.002%.
3. **The entropy raw rule is the main problem.** It stores windows raw whenever their bytes look random, even when
   that data repeats earlier data and would compress well. It cost 8% to 31% on four of the five files above. Storing
   the source of a repeat raw also removes it from the compressed stream, so later copies of it compress worse.
   `--raw off` beats the 1.2 rule on every file. Against `--sets 0` it wins on the three files with interleaved
   content and loses by 0.1–0.45% on Silesia and the archive.
4. **`--raw test` fixes most of the loss** on the switching files, but it cannot know about repeats that come later or
   lie beyond its 2 MB window (dup_random_chunks.bin), so letting zstd decide (`--raw off`) is simpler and safer.
5. **For duplicated content, zstd `--long` is the right tool**: 44% smaller than any block-split variant on
   dup_random_chunks.bin. Sets help there only by bringing some copies closer together.

## A smoothing issue

In 1.2, `labels()` replaces any run shorter than 8 windows with the label of the run before it. When k-means splits
homogeneous data into several clusters, which happens with near-identical windows, the result is many short runs.
The first short run after some raw windows takes the RAW label, the next short run copies it, and so on, so a whole
compressible stretch can end up stored raw. In dup_random_chunks.bin every window, text included, ends up raw and the
file is not compressed at all. `--smooth safe` keeps compressible runs out of RAW. Two further improvements to consider:
merge clusters whose centres are close (so homogeneous data stays in one set), and pick the number of sets by how much
clustering actually separates the windows.

## Suggestions

- Make `--raw off` the default, or use a trial-compression test, and keep the safe smoothing rule.
- Report results against `--sets 0` at the same block size: that is the fair baseline for Sets.
- Look for real data where types are interleaved at the megabyte scale, since that is where Sets can win.
- Include zstd `--long` and 7-Zip-style grouping by file type as comparisons.

## Files

- `hs_zstd.cpp`: the patched source (1.3-ablation), MIT licence as in 1.2.
- `build.sh`: builds hs_zstd with zstd 1.5.6.
- `run_ablation.py`: the comparison runner.
- `make_testfiles.py`: the two seeded stress files.
