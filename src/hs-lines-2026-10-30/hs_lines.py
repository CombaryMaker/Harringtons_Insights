#!/usr/bin/env python3
"""Harrington Sets, line-record variant: multi-core use for any single-core tool that reads records (one per line)
on standard input and writes its results to standard output (jq, streaming learners, filters).
Original method by Michael Hugh Harrington, 2026. MIT License.

Same method as hs_wrap.py, with two changes for record streams: windows are about 64 KB but always end at a line
end, and a marker line is placed before every window in each block, so each window's output can be cut out of
the tool's output and put back in original order. The tool must copy marker lines through unchanged.

Usage: hs_lines.py IN OUT --tool "jq -c -f filter.jq" [--jobs N] [--block-mb B] [--mode sets|blocks]
       [--marker '{"__hsw":%d}']
"""
import argparse, math, os, shlex, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
W = 65536; RAW = 255
def features(c):
    h = np.bincount(np.frombuffer(c, dtype=np.uint8), minlength=256); L = len(c)
    p = h[h > 0] / L; e = float(-(p * np.log2(p)).sum())
    alpha = h[65:91].sum() + h[97:123].sum(); digit = h[48:58].sum(); space = h[32] + h[10] + h[13] + h[9]
    high = h[128:].sum(); ctl = h[:32].sum() + h[127] - h[10] - h[13] - h[9]
    return [ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8], e
def windows(data):
    out = []; p = 0
    while p < len(data):
        q = data.find(b'\n', p + W - 1); q = len(data) if q < 0 else q + 1
        out.append((p, q)); p = q
    return out
def labels(data, win):
    nw = len(win); F = np.zeros((nw, 7)); lab = np.zeros(nw, dtype=np.int32); idx = []; size = np.array([q - p for p, q in win])
    for i, (p, q) in enumerate(win):
        F[i], e = features(data[p:q])
        if e > 7.99 and q - p >= 4096: lab[i] = RAW
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
        sz = np.bincount(a, weights=size[idx], minlength=K); big = int(np.argmax(sz))
        lab[idx] = [big if sz[x] < (1 << 20) else x for x in a]
    for _ in range(2):
        i = 0
        while i < nw:
            j = i
            while j < nw and lab[j] == lab[i]: j += 1
            if j - i < 8 and i > 0: lab[i:j] = lab[i - 1]
            i = j
    return lab
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('IN'); ap.add_argument('OUT'); ap.add_argument('--tool', required=True)
    ap.add_argument('--jobs', type=int, default=os.cpu_count()); ap.add_argument('--block-mb', type=float, default=16)
    ap.add_argument('--mode', default='sets', choices=['sets', 'blocks']); ap.add_argument('--marker', default='{"__hsw":%d}')
    a = ap.parse_args(); t0 = time.time()
    data = open(a.IN, 'rb').read(); win = windows(data)
    lab = labels(data, win) if a.mode == 'sets' else np.zeros(len(win), dtype=np.int32)
    blocks = []; B = a.block_mb * (1 << 20)
    for s in sorted(set(lab.tolist())):
        ws = [i for i in range(len(win)) if lab[i] == s]
        if s == RAW: blocks += [[i] for i in ws]; continue   # raw windows: tool still runs, nothing to model
        tot = sum(win[i][1] - win[i][0] for i in ws); nb = max(1, math.ceil(tot / B)); per = tot / nb
        cur = []; acc = 0; k = 1   # equal split by bytes, at window boundaries
        for i in ws:
            cur.append(i); acc += win[i][1] - win[i][0]
            if acc >= per * k and k < nb: blocks.append(cur); cur = []; k += 1
        if cur: blocks.append(cur)
    t1 = time.time(); outs = {}
    with tempfile.TemporaryDirectory() as td:
        def run(k):
            fn = os.path.join(td, f'b{k}')
            with open(fn, 'wb') as f:
                for i in blocks[k]: f.write((a.marker % i).encode() + b'\n'); f.write(data[win[i][0]:win[i][1]])
            with open(fn, 'rb') as f: r = subprocess.run(shlex.split(a.tool), stdin=f, stdout=subprocess.PIPE, check=True)
            cur = None
            for line in r.stdout.splitlines(keepends=True):
                s = line.rstrip(b'\r\n')
                if s.startswith(b'{"__hsw":'): cur = int(s[9:-1]); outs[cur] = []
                else: outs[cur].append(line)
        with ThreadPoolExecutor(a.jobs) as ex: list(ex.map(run, range(len(blocks))))
    with open(a.OUT, 'wb') as o:
        for i in range(len(win)): o.writelines(outs[i])
    print(f'mode={a.mode} windows={len(win)} sets={len(set(lab.tolist()))} blocks={len(blocks)} '
          f'runs={1 + int((np.diff(lab) != 0).sum())} classify={t1 - t0:.2f}s total={time.time() - t0:.2f}s', file=sys.stderr)
if __name__ == '__main__': main()
