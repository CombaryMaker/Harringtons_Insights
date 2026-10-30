for f in nist/bin/biased-random-bytes.bin nist/bin/normal.bin ringOsc_packed.bin biased_bits_packed.bin truerand_4bit_packed.bin synthetic_5bit_skewed.bin synthetic_12bit_noise.bin mtd/BDF/multiple_annotation_chans.bdf; do
  python3 hew2.py $f $(basename $f) 1048576 2>&1 | grep -v '^skip'
done
