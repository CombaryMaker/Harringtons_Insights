lpaq1-h: a Harrington mutant of Matt Mahoney's lpaq1 (2007).

Licence: lpaq1 is GPL (version 3 or later), so lpaq1h.cpp is GPL too. It is NOT covered by the
repository's MIT licence. The original copyright notice is kept in the source.

Changes (all switched by per-file flags stored in the header's version byte):
  4  match recovery: a match of 24+ bytes survives one wrong byte
  8  binary mode: HCM's record-length detector runs on the byte history
  1  record contexts (byte above + above-right) replace the word context
  2  record contexts (rows above + last byte) also replace the order-6 context
Classification: MUTANT. Any file that uses one of the additions (flags other than 0) needs the
lpaq1-h decoder; stock lpaq1 cannot read it. Only files where no addition helps (flags 0, e.g. plain
text) are written as standard lpaq1 files, byte-identical to stock lpaq1 output and readable by it.

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

Results on Andrew Goodwin's three benchmark sets (29 Oct 2026; sets.py, full data in
lpaq1h_goodwin_sets.json). Sizes in bytes; ratio = original / compressed. Compression time is the
total of all six flag tries; decode time is one in-order run of every file, wall clock including
program start. Every file round-tripped exactly. Other compressors' figures are Goodwin's
(results/corpora_cloud.json, single 2.1 GHz Xeon core), so their times are from a different machine.

  Set (original bytes)          lpaq1-h      ratio  compress  decode   | lpaq1 -6   HCM max    ZPAQ -m5 (data only)
  Canterbury (2,810,784)        359,786      7.81   25.7 s    4.1 s    | 392,132    350,408    350,627
  Silesia 1 MB sample (12,582,912) 3,559,695 3.53   91.3 s    15.9 s   | 3,643,900  3,562,421  3,418,967
  Synthetic set (17,999,934)    8,113,745    2.22   146.8 s   25.6 s   | 8,656,077  8,670,602  8,202,053

Notes that stand out:
- Synthetic set: lpaq1-h is the smallest of every compressor Goodwin tested, ZPAQ -m5 included
  (8,113,745 vs 8,202,053 bytes), at about 0.7 MB/s decode.
- Silesia sample: smaller than both HCM modes and lpaq1; ZPAQ -m5 still smallest.
- Canterbury: third, behind HCM and ZPAQ -m5; HCM's text contexts are the difference.
- Decode speed is the same as stock lpaq1 on this machine (Canterbury 4.21 s lpaq1 vs 4.12 s lpaq1-h).
- Compression is about 6x slower than lpaq1 because six flag settings are tried per file; a
  detector-chosen setting would remove most of that.

PARALLEL BLOCKS (29 Oct 2026): lpaq1h_par.py cuts a file into blocks (2 MB in the test), compresses each with
lpaq1-h best-of in parallel and decodes the blocks in parallel (format "LPHP"). 2-core machine, exact:
  reymont 157,486 -> 163,049 (1,000,000 bits down to), decode 6.7 -> 4.1 s, compress 38.9 -> 28.2 s
  x-ray   438,616 -> 444,765, decode 12.5 -> 6.5 s, compress 69.0 -> 36.6 s
  dickens 215,212 -> 227,103, decode 12.4 -> 7.0 s, compress 69.7 -> 41.6 s
Note: text loses most (dickens +5.5%) because every block starts with an empty model; larger blocks lose less.
