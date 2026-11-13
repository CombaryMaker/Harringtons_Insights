h3d - LOSSLESS 3D COMPRESSION FOR CT / MRI SLICE SERIES (proof of concept, version 1.0, 13 November 2026)
Michael Hugh Harrington. MIT License (LICENSE.txt).

WHAT IT DOES
Medical scans are stored as a stack of slices, and most compressors (including JPEG XL and JPEG-LS inside DICOM)
code each slice on its own. h3d codes the stack as one 3D volume: every pixel is also predicted from the slice in
front of it. Whole DICOM files go in, and the decoder rebuilds every file byte for byte, headers included.

HOW IT WORKS
  1. Seven simple predictors guess each pixel from pixels already decoded: the left/up median (as in JPEG-LS), a 3D
     median using the slice in front, the slice in front plus the change seen in this slice, the slice in front,
     gradient, average, and the pixel above.
  2. The guesses are blended. Each predictor's weight is 1 / (1 + its recent error)^2, where its recent error is how
     wrong it was on the neighbouring pixels (left, above, above-left, above-right) and, for the 3D predictors, on the
     same pixel in the slice in front.
  3. The error (pixel minus blended guess) is coded with an adaptive binary arithmetic coder. Its context is how big
     the errors were around the pixel in this slice, how big they were in the slice in front, and how much the
     predictors disagree (1,728 contexts).
  4. The rest of each DICOM file (header and anything after the pixels) is coded with the same arithmetic coder,
     using the previous file's header at the same position as context, so a series of near-identical headers costs
     almost nothing.
  All arithmetic is integer, so every computer decodes exactly the same. Files that are not plain 16-bit greyscale
  DICOM are kept whole and coded as bytes, so nothing is ever lost.

RESULTS (cloud machine, 2 cores; every h3d result decoded and compared byte for byte; image codecs were given the
pixels, and the rest of each file was added with xz -9e, which is fair to them)
  Scan (public)                                bytes          h3d          JPEG XL e9   JPEG-LS      xz -9e
  Lukas 3D DICOM, chest CT, 341 slices, 1 mm   179,819,308    61,129,865   61,955,540   63,262,945   -
  dcm_qa_ct GE human CT, 28 slices, 4 mm        14,734,346     2,667,330    2,348,120    3,014,665    4,436,844
  dcm_qa_ct Philips phantom, 141 slices, 1 mm   74,651,466    14,009,387   13,165,160   16,171,779   22,392,960
  dcm_qa_ct Philips, 59 slices, 2.5 mm          30,850,072     5,088,559    4,635,172    5,805,135    8,481,892
  (Lukas: JPEG XL e9 = libjxl 0.12 with --buffering=0 on each slice; JPEG-LS = CharLS. HX Complete on the same
  set: 64,649,904 bytes.)

  Times (seconds)                              h3d compress / decode   JPEG XL e9 compress / decode   JPEG-LS
  Lukas 3D DICOM                                18.0 / 10.5             -                             3.3 / 3.1 (pixels)
  dcm_qa_ct GE                                   0.7 / 0.6               8.1 / 0.5                     0.1 / 0.1
  dcm_qa_ct Philips 1 mm                         3.4 / 3.5              36.1 / 2.4                     0.7 / 0.6
  dcm_qa_ct Philips 2.5 mm                       1.4 / 1.4              16.3 / 1.0                     0.3 / 0.3

  h3d is smaller than JPEG-LS (the lossless codec hospitals most often use) on every scan, by 5 to 13 percent, and
  smaller than JPEG XL on the Lukas chest CT. On the three dcm_qa_ct series JPEG XL is 6 to 12 percent smaller:
  its model of each single slice is stronger than h3d's, and that matters more than the slice-to-slice gain when
  the scan is less noisy or the slices are far apart. h3d compresses 10 to 25 times faster than JPEG XL at effort 9
  and decodes about 1.4 times slower. Released as a proof of concept: combining h3d's slice-to-slice prediction with
  a stronger single-slice model is the open next step.

BUILD AND USE
  gcc -O2 -o h3d src/h3d.c            (any C99 compiler; no libraries needed)
  h3d c scan.h3d slice001.dcm slice002.dcm ...    give the files in slice order
  h3d d scan.h3d output-folder                     rebuilds every file byte for byte
  Memory: about 50 MB plus about 40 bytes per pixel of one slice.

TEST DATA
  Lukas corpus, lukas_3d_dicom.zip: https://www.data-compression.info/Corpora/LukasCorpus/
  dcm_qa_ct: https://github.com/neurolabusc/dcm_qa_ct (series In/GE, In/Philips/S21570/S2020, In/Philips/S21610/S3010)
  test/test_h3d.py repeats the dcm_qa_ct comparison (needs python3, numpy, pydicom, imagecodecs; files sorted by
  InstanceNumber).

BACKGROUND AND CREDIT
  Predicting a slice from its neighbour, blending predictors by their recent errors, and choosing coding contexts
  from local error size are established techniques (3D medical image coding, CALIC, JPEG-LS, and the weighted
  predictor of JPEG XL). h3d is a simple, small, open combination of them for DICOM series, with exact rebuild of
  the original files.
