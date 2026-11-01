# hs_zstd — parallel decompression of one large file with stock zstd frames

`hs_zstd` stores a large file as independent zstd frames (zstd 1.5.6, unmodified, linked in) and decodes them on every
core straight into a memory-mapped output. Windows are optionally grouped by byte statistics first (Harrington Sets),
which only matters for size on files whose content type changes inside the file. See `ABOUT.txt` for the method,
prior work and credits.

**4 GB public Silesia file, 6-core / 12-thread desktop, every decode verified exact:**

| tool | compressed bytes | ratio | compress | decode median | decoder memory |
|---|---|---|---|---|---|
| zstd -3, one thread | 1,264,836,194 | 3.184 | 14.7 s | 3.73 s | ~10 MB |
| zstd -3 -T12 | 1,264,836,194 | 3.184 | 2.9 s | 4.25 s | ~10 MB |
| pzstd -3 -p 12 | 1,268,506,060 | 3.174 | 4.1 s | 2.85 s | ~160 MB |
| **hs_zstd 1.3, 4 MB blocks, 12 jobs** | 1,270,027,824 | 3.171 | 2.9 s | **2.27 s** | ~130 MB |
| hs_zstd 1.3, 16 MB blocks, 12 jobs | 1,265,694,589 | 3.182 | 3.5 s | 2.51 s | ~300 MB |

Input 4,026,833,020 bytes; ratio = input size / compressed size (higher is better).

hs_zstd's decode times include forcing the output to disk (`--flush`); the other tools' do not. Core scaling,
a 3.2 GB public mixed tar, the Sets-vs-plain-blocks ablation and a 2 GB race are in the `results-*.txt` files.

## Files

| file | what |
|---|---|
| `hs_zstd.cpp` | source, version 1.3 (C++17; needs zstd's `lib/`) |
| `hs_zstd.exe` | Windows x64 build, zstd 1.5.6 built in, no DLLs |
| `ABOUT.txt` | method, what the results show, credits, references |
| `METHODOLOGY.txt` | machine, data, timing and verification rules for every result here |
| `results-*.txt` | raw logs of each run |
| `test-data-sources-and-hashes.txt` | download URLs and SHA-256 of the public archives in the mixed tar |

## Use

```
hs_zstd c big.tar big.hsz --jobs 12 --block-mb 4        # compress (level 3 default; --level L)
hs_zstd d big.hsz big.tar --jobs 12                      # decompress
hs_zstd c big.tar ctl.hsz --jobs 12 --sets 0             # control: plain block splitting, no grouping
```

## Build

```
make -C zstd/lib libzstd.a
g++ -O2 -std=c++17 -pthread hs_zstd.cpp -Izstd/lib zstd/lib/libzstd.a -o hs_zstd
```

## Limits

The `.hsz` container needs hs_zstd to decode (each block inside is a standard zstd frame). Decoder memory is
larger than zstd's. Results are from one Windows desktop. Repeats farther apart than one block are lost
(`zstd --long` handles those better).

## License

MIT License, Copyright (c) 2026 Michael Hugh Harrington. zstd is © Meta Platforms, Inc. and affiliates, BSD/GPL-2.
