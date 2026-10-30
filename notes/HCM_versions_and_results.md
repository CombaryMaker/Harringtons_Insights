# Harrington Compression Method (HCM) — Versions and Results
Compiled 2026-10-26. Sizes are "1,000,000 bits down to X bits" unless shown as bytes.
Test set: Canterbury corpus (11 files) unless noted. All round trips decoded exactly.

## Rules and naming
- No-pain rule: no loss of compression or decode time allowed, only gains to either or both.
- Ternary version = "hcm-trit" + version number, separate program.
- Binary route with record contexts = HCM-Bin v1 (original HCM kept undisturbed).
- Breakthrough methods get "Harrington" + type (e.g. Harrington Transform); otherwise "Harrington Insights".
- Originality: work already done by others doesn't count; a very minor change on a known idea is acceptable.
- License: MIT with Michael's copyright. Published: https://github.com/CombaryMaker/Harringtons_Insights

## Early Python versions (200 KB Python docs text / 140 KB gate log)
- Restore point "HCM Route 2" (Oct 24): BWT (32-bit index) > MTF > zero-run tokens (symbols 256/257, terminator 258) > adaptive arithmetic coder over 259 symbols, previous-token context (counts start 1, +24/hit, halve above 65,536; 32-bit Witten-Neal-Cleary). Header = 32-bit length + 32-bit BWT index.
  - Text 35,378 bytes (176,890); gate log 12,081 bytes (86,293). Beat xz (37,048 / 14,688); trailed bzip2 (33,770 / 11,125), ZPAQ -m5 (28,101 / 8,469).
- Context-balance coder (Oct 24), file hcm_context_balance.py: per-bit balances, hashed tables for last 1,2,3,4,6 bytes + partial byte + current word; long-match predictor (6-byte hash); logistic mixer (partial byte x match bucket, rate 0.008); secondary estimate by previous byte + partial byte; 32-bit arithmetic coder.
  - Text 29,725 bytes (148,625); gate log 8,447 bytes (60,336). ~27 s each way on 200 KB.
  - Ablation (bytes added, text / gate log): remove long match +2,688 / +315; stop mixer learning +1,969 / +1,413; remove word context +808 / +20; tables 2^18 +614 / +47; remove refinement +403 / +210; tables 2^20 +145 / +5; blend 50/50 +56 / +41; verified matches -36 / +5; blend 90/10 -7 / -28; verified + 90/10 together 29,684 / 8,428.
- Top 10 (Oct 24, 200 KB text, share of original / encode time): 1) context-balance 14.86%, 27 s; 2) restore point 17.69%, 0.9 s; 3) BWT>MTF>Sort8>arith 20.45%, 3.1 s; 4) BWT>MTF>zero runs>arith 21.47%, 0.8 s; 5) BWT>MTF>zero runs>Huffman>HaBiTS 5-trit 21.70%, 0.8 s; 6) BWT>MTF>zero runs>Huffman 22.39%, 0.5 s; 7) per-context Huffman 4-byte>HaBiTS 23.74%; 8) 3-byte 24.38%; 9) predictor ranks 3-byte>Huffman 26.90%; 10) 2-byte>HaBiTS 27.79%. Refs: ZPAQ -m5 14.05%, bzip2 16.89%, xz 18.52%.

## HCM v2 C++ port (Oct 24-25)
- Byte-identical to Python v2 on all 11 files. Fair ZPAQ run: v2 won 5 of 11. All 11: 148,355.

## HCM v3 (Oct 25) — hcm.cpp, HCM.exe (fast) / HCM-Max.exe (max), commands c/x/d
- Orders 1,2,3,4,6,8 + word + word pair; 64-byte nibble blocks, 2-way; verified long match; integer mixer; max mode adds 2nd mixer; APM; integer-only coding (deterministic). File = "HCM3" + mode + length + CRC-32. Reads v2 files.
- All 11: fast 147,504 / max 146,695 / ZPAQ -m5 128,716. Text files: fast 247,798 / max 245,263 / ZPAQ 248,931.
- Wins 6 of 11 vs ZPAQ/xz/bzip2. ~4x faster than v2; ~6.3-6.5x faster than ZPAQ. RAM fast 42 MB, max 68 MB.

## hcm-trit v1 (Oct 25)
- Same program as v3 (exe named with "trit", command t). Each byte = 6 base-3 digits of (byte XOR flip); tries 5 patterns. Header "HCMT".
- ptt5 43,748 bytes (v3 45,083); kennedy 53,007 (53,899); text 0.4-1% worse.

## Ternary pointer ("2 = pointer", Michael's idea) — becomes hcm-trit v2
- First version: all 11 145,901 (v3 fast 147,504). kennedy 53,899 -> 50,070 bytes. Source: cpp_port/trit/ptr2_bench.cpp.
- Improved (no-match candidate from 4-byte context, 4th mixer input): 145,307. Source: ptr3_bench.cpp.
- Speed-recovered (orders 1,2,3,4,6): 145,827 at ~v3 speed. Max mode: 144,691. Source: ord_12346.cpp.
- On bzip2 front end: real bzip2 -9 193,081 -> 179,074. Source: cpp_port/bzp/bzp.cpp.

## HCM-Bin v1 (Oct 25) — record contexts for binaries
- kennedy 16,763 bytes (ZPAQ 16,822, xz 51,868); ptt5 27,623 (ZPAQ 28,563); sum 9,726 (xz 9,500). Source: cpp_port/trit/bin_bench.cpp.

## HCM v3.1 (Oct 25) — binary-only record swap, no added pain
- All 11: fast 128,467 / max 127,177. Source: v3swap_bench.cpp.
- Same swap: hcm-trit v1.1 128,687; hcm-trit v2 5-order 128,105, 6-order 127,448, max 126,256; HCM-Bin v1 ~127,225.

## HCM v3.2 (Oct 25) — mixer selected by word position
- All 11: fast 127,681 / max 126,756; hcm-trit v1.2 127,827; hcm-trit v2 5-order 127,462 / 6-order 126,829 / max 125,901. Source: txt_bench.cpp.

## HCM v3.3 (Oct 25) — line-position context (published on GitHub)
- All 11: fast 125,763 / max 124,675. Wins 9 of 11 vs best rival. hcm-trit v2 5-order 125,625 / 6-order 124,940 / max 123,857 (best overall).
- Strict no-pain: per-file fallback to v3, no file worse; decode time no measurable loss.
- With cmix English dictionary pre-step: v3.3 fast 120,010; hcm-trit v2 max 118,283 (dictionary is GPL-3, English-only).

## No-pain tuned standard formats (stock decoders read output)
- xz (xz-hi): 175,424 -> 164,663; every file smaller and faster to decode. kennedy 51,868 -> 23,656 bytes.
- bzip2 (bzip2-hi): 193,081 -> 189,235; every file smaller and faster.

## paq8px / cmix lab (Oct 25)
- paq8px "lite": ~2x faster, 107,768 vs 107,450. cmix run in 2.5-4 GB instead of 32 GB, no size change on fields.c.

## Harrington Exchange / Combary front ends (Oct 26)
- Record-length delta (13) on kennedy: zstd 64,815 -> 26,398 bytes; lz4 324,136 -> 43,654; gzip 209,721 -> 42,972. Passes no-pain with SIMD reverse-delta.
- Combary activity map before gzip: kennedy 209,721 -> 35,558 bytes, faster decode (passes no-pain on gzip only).
- Permutation-header trick: 256-byte table 256 -> 225 bytes (shrinking header) -> 211 (full factorial index, known method).

## Cost model (8 text files, per TB)
- Transfer + decode electricity: v3 fast $23.35 (cheapest); bzip2 $25.65, xz $28.71, ZPAQ $28.09.
- Binaries, per million customers: HCM $11.39, ZPAQ $18.44, xz $19.76.

## Note on source files
Only the GitHub release (HCM v3.3, xz-hi, bzip2-hi, Windows exes) is included as code. Experimental sources named above (ptr*_bench.cpp, bin_bench.cpp, txt_bench.cpp, etc.) lived in earlier session workspaces that are no longer available.
