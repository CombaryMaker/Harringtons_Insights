#!/usr/bin/env python3
"""Generates two small stress-test files for the raw rule (needs numpy). Both are seeded, so everyone gets the same bytes.

  dup_random_chunks.bin  (48 MB)  eight 2 MB chunks of random bytes (stand-ins for JPEG, zip or encrypted files), each
                                  stored twice, with 2 MB of ordinary text between: high byte entropy, but half of it
                                  repeats earlier data within zstd's reach.
  switch_1mb.bin         (24 MB)  switches every 1 MB between text, fixed-length records, random bytes and
                                  random-looking stretches that repeat earlier data.
Usage: python make_testfiles.py [outdir]
"""
import os, sys, numpy as np
out = sys.argv[1] if len(sys.argv) > 1 else "."; os.makedirs(out, exist_ok=True); rng = np.random.default_rng(20261101)
words = [rng.choice(np.frombuffer(b"etaoinshrdlucmfwypvbgkjqxz", np.uint8), rng.integers(2, 10)).tobytes() for _ in range(5000)]
p = 1 / np.arange(1, 5001) ** 1.05; p /= p.sum()
def text(n):
    s = b" ".join(words[i] for i in rng.choice(5000, n // 4, p=p)); return (s * (n // len(s) + 1))[:n]
def records(n):
    r = np.zeros((n // 13 + 1, 13), np.uint8); r[:, 0:4] = np.arange(len(r), dtype=">u4").view(np.uint8).reshape(-1, 4); r[:, 4:6] = 0x41
    r[:, 6:9] = (500000 + np.cumsum(rng.integers(-1, 2, len(r)))).astype(">u4").view(np.uint8).reshape(-1, 4)[:, 1:]; r[:, 9:13] = rng.integers(0, 256, (len(r), 4)); return r.tobytes()[:n]
def random(n): return rng.integers(0, 256, n, dtype=np.uint8).tobytes()
def repeats(n):
    base = bytearray(random(65536)); out_ = bytearray(base)
    while len(out_) < n:
        L = int(rng.geometric(1 / 300)); src = int(rng.integers(0, max(1, len(out_) - L))); seg = bytearray(out_[src:src + L])
        for i in np.nonzero(rng.random(len(seg)) < 0.01)[0]: seg[i] = int(rng.integers(0, 256))
        out_ += seg
    return bytes(out_[:n])
with open(os.path.join(out, "dup_random_chunks.bin"), "wb") as f:
    for k in range(8):
        c = random(2 << 20); f.write(c); f.write(text(2 << 20)); f.write(c)
kinds = [text, records, random, repeats]
with open(os.path.join(out, "switch_1mb.bin"), "wb") as f:
    for i in range(24): f.write(kinds[int(rng.integers(0, 4))](1 << 20))
print("wrote dup_random_chunks.bin and switch_1mb.bin in", out)
