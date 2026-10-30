# Tools archive — 28 October 2026

This folder holds every script and source file Claude wrote or modified for the compression research: harnesses, transforms, inverses, benchmarks and book builders. It does not hold test data or results. Paths mirror the working folder.

## Third-party tools, rebuilt from public source
| Tool | Source | Build |
|---|---|---|
| zstd 1.6.0-dev, lz4 1.10.1 | github.com/facebook/zstd, github.com/lz4/lz4 | make |
| 7-Zip (7zz) | github.com/ip7z/7zip | CPP/7zip/Bundles/Alone2: make -f ../../cmpl_gcc.mak |
| Brotli 1.2.0 | github.com/google/brotli | cmake, Release |
| libjxl (cjxl/djxl) | github.com/libjxl/libjxl (recursive) | cmake Release, BUILD_TESTING=OFF |
| CharLS (JPEG-LS) | github.com/team-charls/charls | cmake Release |
| Snappy | github.com/google/snappy | cmake |
| LZFSE | github.com/lzfse/lzfse | make |
| Apache ORC C++ | github.com/apache/orc | cmake |
| FLAC | github.com/xiph/flac | cmake |
| libwebp | chromium.googlesource.com/webm/libwebp | cmake |
| DuckDB 1.3.2 | duckdb.org CLI release | — |
| RAR 7.30 beta 2 | rarlab.com Linux x64 | — |
| xz 5.4.5, gzip 1.12, bzip2 1.0.8, qpdf, poppler | system packages | — |

## Key programs (author's code)
- modern/bin/hcmx.cpp + px.inc — HCM v3.3 / hcm-trit v2 / HCM-Bin best-of (g++ -O2 -march=x86-64-v2). HCMX_ONLY=<mode bits> forces a variant: 32 = v3.3, 96 = trit2, 240 = Bin1 (+16 for binaries).
- modern/bin/undelta.c, actbuild.c, memrun.c — record-delta inverse, activity-map rebuild, timing/memory wrapper.
- rv/rvb.cpp — in-process record delta / activity map benchmark (zstd, zlib, xz, Snappy). FASTU=1 selects the new prefix-sum inverse for R = 1, 2, 4, 8.
- rv/*.py — the 28 Oct random re-verification harness (seed in draw.json).
- winbuild/xz_hi.cpp, bzip2_hi.cpp, bz/, bz2lab/tuned/ — xz-hi and bzip2-hi.
- pdf/hi.py, rend*.py — PDF row-filter rewrite and render checks.
- pq/*.py, ob*.cpp — Parquet and ORC column-layout tests.
- sn/sb.cpp, lf/lb.cpp — Snappy and LZFSE benches.
- av/*.py — FLAC, WebP, PNG searches.
- he/, juggle.py, hss/ — Harrington Exchange and 16HE tables.
- book2pdf/build.py, mk.js, style.css — Book 2 PDF build.
