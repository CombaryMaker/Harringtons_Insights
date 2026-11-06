#!/usr/bin/env python3
# make_mixed.py - builds the public "mixed" test file used for the Sets test: 1 MB pieces taken in turn from five
# Silesia corpus files (dickens, mozilla, nci, sao, x-ray), 8 pieces from each, so the data type changes every 1 MB.
# Usage: python3 make_mixed.py SILESIA_DIR OUT
# Michael Hugh Harrington, 2026, MIT License.
import sys, os, hashlib
d, out = sys.argv[1], sys.argv[2]
names = ["dickens", "mozilla", "nci", "sao", "x-ray"]
data = {n: open(os.path.join(d, n), "rb").read() for n in names}
MB = 1 << 20
buf = bytearray()
for i in range(8):
    for n in names:
        buf += data[n][i * MB:(i + 1) * MB]
open(out, "wb").write(buf)
print(out, len(buf), "bytes sha256", hashlib.sha256(buf).hexdigest())
