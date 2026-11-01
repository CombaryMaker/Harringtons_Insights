Core-scaling curve, 2026-11-01, desktop Ryzen 5 7500X3D (6 cores / 12 threads), 15 GB RAM, NVMe, Windows 11.
File: silesia_4gb.bin (12 Silesia files joined, x19, 4,026,833,020 bytes, public). zstd level 3. 3 decode rounds per
point (median), first decode of each point SHA-256 checked - all 18 exact. hs_zstd decodes with --flush (output forced to
disk before the clock stops); pzstd does not flush, so its timings stop while data may still sit in the write cache.

Decode median, seconds (jobs 1 / 2 / 4 / 6 / 8 / 12):
  hs_zstd 1.3, 4 MB blocks    7.52 / 4.87 / 3.24 / 2.71 / 2.55 / 2.43   (3.1x from 1 to 12 jobs)
  hs_zstd 1.3, 16 MB blocks   7.88 / 5.07 / 3.34 / 2.97 / 6.88* / 3.96*  (*noisy rounds: 2.49 / 9.80 / 6.88 at 8 jobs)
  pzstd -3                    4.77 / 4.04 / 3.60 / 3.78 / 3.49 / 3.53   (1.35x from 1 to 12 jobs)
Compress, seconds (jobs 1 / 2 / 4 / 6 / 8 / 12):
  hs_zstd 1.3, 4 MB blocks    19.59 / 10.45 / 5.02 / 3.99 / 3.28 / 2.89  (6.8x)
  hs_zstd 1.3, 16 MB blocks   20.50 / 11.43 / 5.74 / 4.38 / 3.76 / 3.18  (6.4x)
  pzstd -3                    15.58 /  8.60 / 5.29 / 4.53 / 4.20 / 3.78  (4.1x)
Size, 1,000,000 bits down to: hs_zstd 4 MB 315,391; 16 MB 314,315; pzstd 315,013 (independent of job count).

Reading: compression scales almost linearly to the 6 physical cores and keeps gaining on hyperthreads. Decoding flattens
after 4-6 jobs at about 2.2-2.7 s for 4 GB with a forced flush, i.e. it becomes disk-write bound (about 1.5-1.8 GB/s), so
past six jobs extra threads buy little. At one job hs_zstd decodes slower than pzstd because hs_zstd's figure includes the
forced write to disk and pzstd's does not; from 4 jobs up the 4 MB hs_zstd is faster than pzstd at every point.
