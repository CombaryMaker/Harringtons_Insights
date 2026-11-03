HCM / Harrington Insights — benchmark tools and raw results, saved 2026-10-27
Each folder holds the program or script used for that test plus its raw results.
zstd_brotli_hi/       zstd-hi and Brotli-hi settings search (hi.c bench, search lists, re-timed results)
lzma_hi_xz_hi/        LZMA -9 king race: lzma-hi (7-Zip LZMA tuning) and xz-hi, Canterbury + Silesia
7z_hi/                7z-hi vs 7-Zip Ultra (-mx9), per-file LZMA2 + filter search, decode timings
tier4_race/           HCM v3.3 vs ZPAQ / bzip3 / BSC on Silesia; pre.c = x86 E8/E9 filter + Combary activity map
image_jxl_jls/        JPEG XL / JPEG-LS medical image tests (search script, JPEG-LS bench, results)
mahoney_race_2026-11-02/  Matt Mahoney test sets race (Calgary, Canterbury large, Generic Compression Benchmark, enwik9):
                          ZPAQ 7.15 / lpaq1 vs HCM, HCM Sets, hcmx, zpaq-hi, lpaq1-h; REPORT.txt has the tables
silesia_record_delta/ record-length detection + delta front end on Silesia (rb.c), fast-level results
Note: the hcm-trit and HCM-Bin source code from Oct 25 was not recoverable; their designs and
results are recorded in HCM_versions_and_results.md and Theories_and_Methods.txt.
