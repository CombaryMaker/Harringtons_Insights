# Research source code

Source code behind the results in the book *Harrington's Insights* (`../Harringtons-Insights.pdf`).
All code is by Michael Harrington (written with Claude) and is under the repository's MIT License.
Test data is not included; every script names its public source (Canterbury and Silesia corpora,
NIST SP 800-90B sample data, MNE BDF test data, rtl_433 test captures, pyulog / pychapter10 /
pymavlink test files). Synthetic files use seed 2360932695.

Note: the research scripts were written for a Linux working folder and contain absolute paths at the
top of each file. Change those paths to your own folders before running.

| Folder | Contents | Book |
|---|---|---|
| `hcmx/` | `hcmx.cpp` + `px.inc`: HCM v3.3, hcm-trit v2 (ternary pointer) and HCM-Bin record contexts, best-of per file. Build: `g++ -O2 -march=x86-64-v2 hcmx.cpp -o hcmx`. Use: `hcmx c IN OUT` (fast), `hcmx x IN OUT` (max), `hcmx d IN OUT` (decode). Env `HCMX_ONLY=<mode bits>` forces one variant: 32 = v3.3, 96 = trit v2, 240 = HCM-Bin (+16 for binaries). `undelta.c`, `actbuild.c`: record-delta inverse and activity-map rebuild. `memrun.c`: time and peak-memory wrapper. | Ch 4, 8, 9–12 |
| `ternary-pointer/` | Development benches for the ternary pointer (hcm-trit): candidate selection, escape modelling, exclusion and ablations. The shipped version is inside `hcmx/`. | Ch 11 |
| `harrington-exchange/` | `he3.py`: Exchange encoder/decoder (any width J ≤ 24, swap S < J; modes full / split / rename / norev). `he3dec.c`, `he3dec2.c`: compiled decoders with timing. `he3run.py` (ablation), `he3sweep.py` (276-point S/J sweep), `prepare.py` (builds the biased test set). `hew2.py`, `juggle.py`, `he_test.py`, `hss16_10.py`: earlier versions and 16HE tables. | Ch 15 |
| `harrington-exchange/baselines/` | Outside-baseline comparison (zstd -22, Brotli 11, 7-Zip, Blosc2, OpenZL, ZPAQ, paq8px, cmix) and the LZ4 tier timing. | 15.12–15.13 |
| `harrington-exchange/blosc-hybrid/` | Exchange → Blosc2 hybrid: search (`mut.py`, `sp2.py`), decoders `mx*.c` (v4/v5 fastest), runners. Needs c-blosc2. | 15.14 |
| `harrington-exchange/telemetry-radio/` | Flight-telemetry and SDR radio tests, Exchange → xz decoder (`he3decxz.c`, needs liblzma), timing script. | 15.15 |
| `record-delta/` | `rvb.cpp`: in-process record delta / activity map benchmark (zstd, zlib, xz, Snappy); `FASTU=1` selects the prefix-sum inverse for R = 1, 2, 4, 8. | Ch 4, 8, App F |

xz-hi and bzip2-hi are in the top-level `xz-hi/` and `bzip2-hi/` folders.
