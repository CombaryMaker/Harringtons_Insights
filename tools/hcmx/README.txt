hcmx - rebuilt 2026-10-27 (originals from Oct 25 were lost)
  hcm-trit v2  = HCM v3.3 + the ternary pointer ("2 = pointer"): one yes/no flag per byte for a
                 candidate byte (match byte, else last byte after the same 4-byte context);
                 on "no" the byte is coded with the candidate excluded.
  HCM-Bin v1   = hcm-trit v2 + 4 added record contexts for binaries (row above + above-right,
                 row above + last byte, rows above 1-2, rows above 1-3).
Files: hcmx.cpp (full program, includes px.inc), px.inc (the new engine), cant.py / sil.py (tests),
       cant.json (Canterbury results). Build: g++ -O2 -march=native -ffp-contract=off -o hcmx hcmx.cpp
Use: hcmx c IN OUT (fast), hcmx x IN OUT (max), hcmx d IN OUT. It tries v3, v3.3, trit2 and
     (binaries) Bin1, and keeps the smallest. Every test file round-tripped byte-exact.
Tuning after first build: candidate table keeps a repeat count (escape cells by count), match-length
escape cells in 8 steps, flag-mixer learning rate >>12 (was >>10).
Canterbury (all 11), 1,000,000 bits down to:
  fast: v3.3 125,763 -> hcm-trit v2 124,984 -> best-of (with HCM-Bin) 124,831   (w/o kennedy 187,988 -> 187,528)
  max:  v3.3 124,675 -> hcm-trit v2 123,896 -> best-of (with HCM-Bin) 123,701   (w/o kennedy 186,168 -> 185,790)
  Old lost versions: trit2 fast 124,940, max 123,857 -> rebuild within 0.04%; max best-of now beats them.
  kennedy.xls bytes: v3.3 18,861 / trit2 17,345 / HCM-Bin v1 16,797 (ZPAQ -m5 16,822; old Bin 16,763)
  ptt5: trit2 27,480 (v3.3 27,511); sum: trit2 9,656 (v3.3 9,726)
  Decode time all 11: fast 2.80 s -> 3.11 s (+11%), max 3.27 s -> 3.70 s (+13%).
q.sh = quick Canterbury size check of one build.
