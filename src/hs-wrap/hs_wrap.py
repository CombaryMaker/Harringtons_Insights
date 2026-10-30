#!/usr/bin/env python3
"""Harrington Sets: multi-core use for any single-core compressor.
Original method by Michael Hugh Harrington, 2026. Code under the MIT License (full text at end of file).

The input is read in 64 KB windows. Windows with byte entropy above 7.99 bits/byte are excluded to a raw
set (stored, never compressed). The rest are grouped by k-means on seven byte features into at most 3 sets,
short runs are smoothed and sets under 1 MB folded into the largest. Each set is joined in file order,
cut into blocks of at most B MB, and every block is compressed by its own copy of the compressor, several
at once (largest blocks first). A run-length map restores every window to its original place, and a CRC-32
of the original is checked after decoding; the container is validated before any decoder runs.

Usage:
  hs_wrap.py c IN OUT --comp "COMPRESSOR {in} {out}" [--jobs N] [--block-mb B]
  hs_wrap.py d IN OUT --decomp "DECOMPRESSOR {in} {out}" [--jobs N]
{in} and {out} become temporary block files; a template without {out} is read from standard output.
Works on Linux, macOS and Windows (on Windows, quote program paths that contain spaces with double quotes).
Memory: about jobs x the compressor's own memory, plus 2-3x the input for this script's buffers; temporary
block files need about the input size again on disk.
"""
import argparse, math, os, shlex, struct, subprocess, sys, tempfile, time, zlib
from concurrent.futures import ThreadPoolExecutor
import numpy as np
W = 65536; RAW = 255; MAGIC = b'HSW2'   # HSW2 = HSW1 + CRC-32 of the original
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
def split_cmd(tmpl, nt=os.name == 'nt'):
    if not nt: return shlex.split(tmpl)
    toks = shlex.split(tmpl, posix=False)   # keeps Windows backslashes; strip the quotes around a token
    return [t[1:-1] if len(t) > 1 and t[0] == t[-1] == '"' else t for t in toks]
def call(tmpl, src, dst):
    args = [t.replace('{in}', src).replace('{out}', dst) for t in split_cmd(tmpl)]
    with open(os.devnull if '{out}' in tmpl else dst, 'wb') as o:   # no {out}: the tool writes to stdout
        r = subprocess.run(args, stdout=o, stderr=subprocess.PIPE)
    if r.returncode:
        msg = r.stderr.decode(errors='replace').strip(); msg = msg if len(msg) <= 400 else msg[:200] + ' ... ' + msg[-200:]
        raise RuntimeError(f'{args[0]} failed (exit {r.returncode}): {msg}')
def run_all(tmpl, pairs, sizes, jobs):   # largest blocks first, so the slowest block never starts last
    order = sorted(range(len(pairs)), key=lambda k: -sizes[k])
    with ThreadPoolExecutor(jobs) as ex: list(ex.map(lambda k: call(tmpl, *pairs[k]), order))
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
        run_all(a.comp, [(f, f + '.z') for f in names], [len(b) for s, b in blocks], a.jobs)
        zs = [open(f + '.z', 'rb').read() for f in names]
    with open(a.OUT, 'wb') as o:
        o.write(MAGIC + struct.pack('<QIII', len(data), zlib.crc32(data), len(R), len(blocks)))
        for s, n in R: o.write(struct.pack('<BI', s, n))
        for (s, b), z in zip(blocks, zs): o.write(struct.pack('<BQQ', s, len(b), len(z)))
        o.write(struct.pack('<Q', len(raw))); o.write(raw)
        for z in zs: o.write(z)
    print(f'{len(data):,} -> {os.path.getsize(a.OUT):,} bytes; sets {sorted(set(lab.tolist()))}, runs {len(R)}, blocks {len(blocks)}, raw {len(raw):,}')
def decompress(a):
    d = open(a.IN, 'rb').read()
    if d[:4] == MAGIC: n, crc, nr, nbk = struct.unpack_from('<QIII', d, 4); p = 24
    elif d[:4] == b'HSW1': (n, nr, nbk), crc, p = struct.unpack_from('<QII', d, 4), None, 20   # older files
    else: raise ValueError('not a Harrington Sets wrapper file')
    R = [struct.unpack_from('<BI', d, p + 5 * i) for i in range(nr)]; p += 5 * nr
    B = [struct.unpack_from('<BQQ', d, p + 17 * i) for i in range(nbk)]; p += 17 * nbk
    rl, = struct.unpack_from('<Q', d, p); p += 8; raw = d[p:p + rl]; p += rl
    # validate the container against the map before running any decoder
    nw = (n + W - 1) // W; need = {}; i = 0
    for s, cnt in R:
        if s != RAW and s > 2: raise ValueError('bad set label in the map')
        if cnt == 0 or i + cnt > nw: raise ValueError('the map does not match the file length')
        need[s] = need.get(s, 0) + cnt * W - (nw * W - n if i + cnt == nw else 0); i += cnt
    if i != nw: raise ValueError('the map does not match the file length')
    if len(raw) != rl or rl != need.get(RAW, 0): raise ValueError('bad raw length')
    got = {}
    for s, ln, zl in B: got[s] = got.get(s, 0) + ln
    if any(s == RAW for s, ln, zl in B) or got != {s: v for s, v in need.items() if s != RAW}:
        raise ValueError('block table does not match the map')
    if p + sum(zl for s, ln, zl in B) != len(d): raise ValueError('wrong file length (truncated or damaged)')
    with tempfile.TemporaryDirectory() as td:
        names = []
        for k, (s, ln, zl) in enumerate(B):
            fn = os.path.join(td, f'b{k}.z'); open(fn, 'wb').write(d[p:p + zl]); p += zl; names.append(fn)
        run_all(a.decomp, [(f, f[:-2]) for f in names], [ln for s, ln, zl in B], a.jobs)
        sb = {}
        for (s, ln, zl), f in zip(B, names):
            b = open(f[:-2], 'rb').read()
            if len(b) != ln: raise ValueError(f'block length mismatch: {len(b)} bytes, expected {ln}')
            sb.setdefault(s, []).append(b)
    sb = {s: b''.join(v) for s, v in sb.items()}
    if raw: sb[RAW] = raw
    pos = {s: 0 for s in sb}; out = bytearray(); i = 0
    for s, cnt in R:
        for _ in range(cnt):
            L = min(W, n - i * W); out += sb[s][pos[s]:pos[s] + L]; pos[s] += L; i += 1
    if len(out) != n or (crc is not None and zlib.crc32(out) != crc): raise ValueError('restored data failed the CRC-32 check')
    open(a.OUT, 'wb').write(out)
if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['c', 'd']); ap.add_argument('IN'); ap.add_argument('OUT')
    ap.add_argument('--comp', default=''); ap.add_argument('--decomp', default='')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2); ap.add_argument('--block-mb', type=int, default=16)
    a = ap.parse_args(); t = time.perf_counter()
    if not (a.comp if a.mode == 'c' else a.decomp): ap.error('--comp is needed to compress, --decomp to decompress')
    try: compress(a) if a.mode == 'c' else decompress(a)
    except struct.error: sys.exit('error: truncated or damaged file')
    except (RuntimeError, ValueError, OSError) as e: sys.exit(f'error: {e}')
    print(f'{"compress" if a.mode == "c" else "decompress"} wall time {time.perf_counter() - t:.1f} s')

# MIT License
#
# Copyright (c) 2026 Michael Hugh Harrington
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
# to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of
# the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
# THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
# CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
