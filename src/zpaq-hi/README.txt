zpaq-hi - ZPAQ 7.15 with a per-block model search (Michael Harrington, Harrington's Insights)

CLASS: Modified. Only the compressor changed. Every archive zpaq-hi writes is an ordinary ZPAQ
archive and is extracted by the unmodified zpaq / libzpaq decoder, because ZPAQ stores the
decompression model inside each block.

WHAT IT DOES
With -m5 (and higher), zpaq-hi builds the stock ZPAQ -m5 model for each block, then also builds
up to three extra candidate models and keeps whichever gives the smallest block:
  1. stock -m5 model (always tried, so the result is never larger than stock)
  2. stock + record contexts, using the record period ZPAQ's own analysis already detects (R):
       row above + above-right       c0,0,(998+R),255,255
       last byte + row above         c0,0,255,(998+R),255
       rows above 1 and 2            c0,0,(999+R),255,(999+R),255
     (the 2-D record contexts from HCM-Bin / HCM v3.1)
  3. stock + a line-position context with the previous byte   c0,1010,0,255
     (the line-position idea from HCM v3.3)
  4. 2 and 3 together
All changes are in compressBlock() in libzpaq.cpp. ZPAQHI_VERBOSE=1 prints every candidate's size.
ZPAQHI_TRY lets you add experimental candidates ("insert|tail" pairs separated by ';').

BUILD
  g++ -O3 -DNDEBUG zpaq.cpp libzpaq.cpp -o zpaq-hi -lpthread

RESULTS - Canterbury corpus, one file per archive, -m5, one run, wall clock incl. start-up.
Every zpaq-hi archive was extracted with STOCK zpaq 7.15 and compared byte for byte (all exact).
  File          stock zpaq bytes  compress  decode | zpaq-hi bytes  compress  decode
  alice29.txt        37,600       0.6 s     0.6 s  |    37,594      0.8 s     0.7 s
  asyoulik.txt       35,379       0.6 s     0.7 s  |    35,374      0.9 s     0.6 s
  cp.html             7,423       0.1 s     0.2 s  |     7,423      0.3 s     0.2 s
  fields.c            3,475       0.1 s     0.1 s  |     3,475      0.2 s     0.1 s
  grammar.lsp         2,078       0.1 s     0.1 s  |     2,078      0.1 s     0.1 s
  kennedy.xls        16,822       4.4 s     4.3 s  |    16,002      9.7 s     5.5 s
  lcet10.txt         90,021       1.6 s     1.7 s  |    89,948      2.1 s     1.9 s
  plrabn12.txt      127,578       1.7 s     2.0 s  |   127,363      2.3 s     2.3 s
  ptt5               28,563       2.0 s     1.8 s  |    27,314      4.1 s     2.4 s
  sum                10,325       0.2 s     0.2 s  |    10,325      0.3 s     0.2 s
  xargs.1             2,528       0.1 s     0.1 s  |     2,528      0.1 s     0.1 s
  TOTAL             361,792      11.5 s    11.8 s  |   359,424     20.9 s    14.1 s
  Compression ratio (original 2,810,784 bytes): stock 7.769, zpaq-hi 7.820.
  1,000,000 bits down to: stock 128,716, zpaq-hi 127,874.

NOTES
- Biggest gains are on record-structured files: kennedy.xls -4.9%, ptt5 -4.4%. Text gains are small
  (lcet10 -0.08%, plrabn12 -0.17%) because ZPAQ -m5 is already strong on text.
- Decoding is slower where an extra model wins (kennedy 4.3 -> 5.5 s, ptt5 1.8 -> 2.4 s): the
  decoder runs whatever model is stored in the block, so a larger model costs decode time. This is a
  size win, not a No-Pain win. Files where the stock model wins decode at stock speed.
- Compression takes about 1.8x as long, since up to four models are tried per block.
- Sizes are full archive sizes as written by zpaq (including its archive headers).

LICENSE: ZPAQ 7.15 is public domain (Matt Mahoney). The zpaq-hi changes are released under the MIT
license (copyright Michael Harrington, 2026), as is the rest of this repository.
