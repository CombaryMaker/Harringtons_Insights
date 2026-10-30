#!/usr/bin/env python3
"""Harrington Sets wrapper: gives any single-core compressor (cmix, paq8px, lpaq, ...) multi-core use.
Original method by Michael Hugh Harrington, 2026. MIT License.

The input is read in 64 KB windows. Windows with byte entropy above 7.99 bits/byte are excluded to a raw
set (stored, never compressed). The rest are grouped by k-means on seven byte features into at most 3 sets,
short runs are smoothed and sets under 1 MB folded into the largest. Each set is joined in file order,
cut into blocks of at most B MB, and every block is compressed by its own copy of the compressor, several
at once. A run-length map restores every window to its original place.

Usage:
  hs_wrap.py c IN OUT --comp "cmix -c {in} {out}" --decomp "cmix -d {in} {out}" [--jobs N] [--block-mb B]
  hs_wrap.py d IN OUT --decomp "cmix -d {in} {out}" [--jobs N]
Memory: peak is about jobs x the compressor's own memory (cmix needs about 32 GB per copy).
"""
import argparse, math, os, shlex, struct, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
W = 65536; RAW = 255; MAGIC = b'HSW1'
def features(c):
    h = np.bincount(np.frombuffer(c, dtype=np.uint8), minlength=256); L = len(c)
    p = h[h > 0] / L; e = float(-(p * np.log2(p)).sum())
    alpha = h[65:91].sum() + h[97:123].sum(); digit = h[48:58].sum(); space = h[32] + h[10] + h[13] + h[9]
    high = h[128:].sum(); ctl = h[:32].sum() + h[127] - h[10] - h[13] - h[9]
    return [ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8], e
def labels(data):
    nw = (len(data) + W - 1) // W; F = np.zeros((nw, 7)); lab = np.zeros(nw, dtype=np.int32); idx = []
    for i in range(nw):
        c = data[i * W:(i + 1) * W]; F[i], e = features(c)
        if e > 7.99 and len(c) >= 4096: lab[i] = RAW
        else: idx.append(i)
    K = 3 if len(idx) >= 48 else (2 if len(idx) >= 16 else 1)
    if idx:
        X = F[idx]; C = [X[0]]
        for k in range(1, K):
            d = np.min([((X - c) ** 2).sum(1) for c in C], axis=0); C.append(X[int(np.argmax(d))])
        C = np.array(C)
        for _ in range(20):
            a = np.argmin(((X[:, None, :] - C[None]) ** 2).sum(2), 1)
            for k in range(K):
                if (a == k).any(): C[k] = X[a == k].mean(0)
        cnt = np.bincount(a, minlength=K); big = int(np.argmax(cnt))
        lab[idx] = [big if cnt[x] * W < (1 << 20) else x for x in a]
    for _ in range(2):
        i = 0
        while i < nw:
            j = i
            while j < nw and lab[j] == lab[i]: j += 1
            if j - i < 8 and i > 0: lab[i:j] = lab[i - 1]
            i = j
    return lab
def runs(lab):
    r = []; i = 0
    while i < len(lab):
        j = i
        while j < len(lab) and lab[j] == lab[i]: j += 1
        r.append((int(lab[i]), j - i)); i = j
    return r
def call(tmpl, src, dst):
    args = shlex.split(tmpl.format(**{'in': src, 'out': dst}))
    if '{out}' in tmpl:   # the compressor writes its own output file
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:                 # the compressor writes to standard output
        with open(dst, 'wb') as o: subprocess.run(args, check=True, stdout=o, stderr=subprocess.DEVNULL)
def compress(a):
    data = open(a.IN, 'rb').read(); lab = labels(data); R = runs(lab); nw = len(lab)
    blocks = []; raw = b''
    for s in sorted(set(lab.tolist())):
        buf = b''.join(data[i * W:(i + 1) * W] for i in range(nw) if lab[i] == s)
        if s == RAW: raw = buf; continue
        nb = max(1, math.ceil(len(buf) / (a.block_mb << 20))); per = math.ceil(len(buf) / nb)
        blocks += [(s, buf[k * per:(k + 1) * per]) for k in range(nb)]
    with tempfile.TemporaryDirectory() as td:
        names = []
        for k, (s, b) in enumerate(blocks):
            fn = os.path.join(td, f'b{k}'); open(fn, 'wb').write(b); names.append(fn)
        with ThreadPoolExecutor(a.jobs) as ex: list(ex.map(lambda f: call(a.comp, f, f + '.z'), names))
        zs = [open(f + '.z', 'rb').read() for f in names]
    with open(a.OUT, 'wb') as o:
        o.write(MAGIC + struct.pack('<QII', len(data), len(R), len(blocks)))
        for s, n in R: o.write(struct.pack('<BI', s, n))
        for (s, b), z in zip(blocks, zs): o.write(struct.pack('<BQQ', s, len(b), len(z)))
        o.write(struct.pack('<Q', len(raw))); o.write(raw)
        for z in zs: o.write(z)
    print(f'{len(data):,} -> {os.path.getsize(a.OUT):,} bytes; sets {sorted(set(lab.tolist()))}, runs {len(R)}, blocks {len(blocks)}, raw {len(raw):,}')
def decompress(a):
    d = open(a.IN, 'rb').read(); assert d[:4] == MAGIC, 'not a Harrington Sets wrapper file'
    n, nr, nbk = struct.unpack_from('<QII', d, 4); p = 20
    R = [struct.unpack_from('<BI', d, p + 5 * i) for i in range(nr)]; p += 5 * nr
    B = [struct.unpack_from('<BQQ', d, p + 17 * i) for i in range(nbk)]; p += 17 * nbk
    rl, = struct.unpack_from('<Q', d, p); p += 8; raw = d[p:p + rl]; p += rl
    with tempfile.TemporaryDirectory() as td:
        names = []
        for k, (s, ln, zl) in enumerate(B):
            fn = os.path.join(td, f'b{k}.z'); open(fn, 'wb').write(d[p:p + zl]); p += zl; names.append(fn)
        with ThreadPoolExecutor(a.jobs) as ex: list(ex.map(lambda f: call(a.decomp, f, f[:-2]), names))
        sb = {}
        for (s, ln, zl), f in zip(B, names):
            b = open(f[:-2], 'rb').read(); assert len(b) == ln, 'block length mismatch'; sb.setdefault(s, []).append(b)
    sb = {s: b''.join(v) for s, v in sb.items()}
    if raw: sb[RAW] = raw
    pos = {s: 0 for s in sb}; out = bytearray(); i = 0
    for s, cnt in R:
        for _ in range(cnt):
            L = min(W, n - i * W); out += sb[s][pos[s]:pos[s] + L]; pos[s] += L; i += 1
    open(a.OUT, 'wb').write(out)
if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['c', 'd']); ap.add_argument('IN'); ap.add_argument('OUT')
    ap.add_argument('--comp', default=''); ap.add_argument('--decomp', default='')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2); ap.add_argument('--block-mb', type=int, default=16)
    a = ap.parse_args(); t = time.perf_counter()
    compress(a) if a.mode == 'c' else decompress(a)
    print(f'{"compress" if a.mode == "c" else "decompress"} wall time {time.perf_counter() - t:.1f} s')
