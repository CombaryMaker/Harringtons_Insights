HCM v3.6.1 decoder memory and time, Silesia (all 12 files, 211,938,580 bytes), fast mode, 2026-11-01, desktop
Ryzen 5 7500X3D 6c/12t, Windows 11. Decoded the .hcm files made by v3.6 on 30 Oct (the 3.6.1 decoder is unchanged).
Peak private memory sampled every 10 ms; all 48 outputs SHA-256 exact.

                     1,000,000 bits down to   decode (sum of 12)   highest peak private memory (one file)
single stream               201,290               86.8 s                   107 MB (mozilla)
parallel blocks 16 MB       202,339               58.8 s                   230 MB (mozilla)
Harrington Sets 16 MB       202,713               47.3 s                   271 MB (mozilla)
Harrington Sets 4 MB        205,939               26.6 s                   549 MB (mozilla)

Memory grows with the number of blocks decoded at once, each with its own HCM model (about 40-50 MB per model):
3.3x faster decode at 4 MB costs about 5x the peak memory of a single stream. Files that fit in one block (mr, ooffice,
osdb, reymont at 16 MB) decode as a single stream and use the same memory as one.

Also re-timed (lost to a harness bug in the fair-gzip run): hs_wrap 1.5 + gzip -6, 4 GB Silesia, 16 MB blocks, 12 jobs:
Sets 24.9 s compress; plain blocks 24.5 s (vs gzip one thread 116.1 s, pigz -p12 13.1 s, bgzip -@12 12.0 s).
