lpaq1-h: a Harrington mutant of Matt Mahoney's lpaq1 (2007).

Licence: lpaq1 is GPL (version 3 or later), so lpaq1h.cpp is GPL too. It is NOT covered by the
repository's MIT licence. The original copyright notice is kept in the source.

Changes (all switched by per-file flags stored in the header's version byte):
  4  match recovery: a match of 24+ bytes survives one wrong byte
  8  binary mode: HCM's record-length detector runs on the byte history
  1  record contexts (byte above + above-right) replace the word context
  2  record contexts (rows above + last byte) also replace the order-6 context
With flags 0 the output is byte-identical to stock lpaq1 and stock lpaq1 decodes it.

Build:  g++ -O2 lpaq1h.cpp -o lpaq1h
Use:    python3 lpaq1h_best.py ./lpaq1h 6 input output      (tries each flag set, keeps the smallest)
        ./lpaq1h d input output                             (decode)

Results (29 Oct 2026, memory option 6, every file round-tripped; one decode run each, wall clock):
  Canterbury total: lpaq1 392,132 -> lpaq1-h 359,786 bytes; decode 4.21 s -> 4.18 s
    kennedy.xls 37,943 -> 21,644   ptt5 43,690 -> 28,416   sum 10,614 -> 9,858   fields.c 2,554 -> 2,537
    text files: identical to lpaq1
  Goodwin synthetic files:
    E_records13 296,842 -> 210,505 (HCM v3.4: 216,752)
    F_rows512   720,244 -> 284,517
    I_repeats   122,848 -> 108,374 (HCM v3.4: 122,404)
    G_audio16   376,764 unchanged (no numeric contexts yet)
