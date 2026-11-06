HX - lossless compression by Michael Hugh Harrington
Patent Pending (U.S. Provisional App. 64/192,843). Not MIT. Free for personal use by individuals and for
non-commercial research, peer review and scientific study; commercial use requires a license. See LICENSE.txt.

HX splits a file into independent segments of about 1 MB, which each decode on their own core. For
every segment it tries a family of reversible transform lines from the Harrington Exchange, such as byte
lanes, 12-bit samples, context lines and audio predictors. Harrington's Sets (64 KB windows sorted into up
to 4 classes) can run first, and the cascade's repeat line handles long repeats. The smallest result is
kept, and the file is stored raw if nothing shrinks it.

Two modes, one program:
  HX Fast      (default)     the Exchange lines, Sets and the repeat line, probe-gated for speed.
  HX Complete  (--complete)  everything HX Fast tries, plus whole-file and split routes with BSC, bzip3,
                             LZMA (with BCJ2 for x86), PPMd and Brotli, and the x86 filter. Optional
                             external routes are HCM and JPEG XL for 16-bit images. A learned router picks the
                             likely best three routes and escalates when a result misses its prediction.
                             --max tries every route.

Usage:  hx c IN OUT [THREADS] [--complete] [--max] [--no-learn]
        hx d IN OUT [THREADS]

Files in src/:
  hx.cpp        the compressor/decompressor (container, segment plan, Sets, routes, split modes)
  entbench.cpp  the Harrington Exchange lines and the coder harness (included by hx.cpp)
  hx_router.h   the learned router (features, nearest-neighbour route prediction, escalation)
  x86f.h        E8/E9 x86 address filter
  hx_lzma.h, hx_bcj2.h, hx_ppmd.h   LZMA, BCJ2 and PPMd routes (wrapping Igor Pavlov's public-domain C code)
See BUILD.txt for how it is built, and All-Software-Test-Results-2026-11-06.txt for results.
