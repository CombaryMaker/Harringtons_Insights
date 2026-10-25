#!/usr/bin/env python3
"""xz-hi: smaller standard .xz files (Harrington Insights).

Compressor-side search only. For each file it tries the LZMA2 literal and
position settings (lc, lp, pb), match-finder effort (nice_len, depth), an
optional pre-filter (delta at the file's record length, which HCM's record
detector finds, or a BCJ filter for executables), a dictionary sized to the
file and a CRC32 check, and keeps the smallest result. The output is a normal
.xz file that any xz decodes.

usage: xz_hi.py INPUT [OUTPUT]          (default OUTPUT = INPUT.xz)
Uses Python's built-in lzma module (liblzma).
"""
import itertools, lzma, sys

def looks_binary(d):
    ctl = sum(1 for c in d if c < 9 or 13 < c < 32 or c == 127)
    return len(d) > 0 and ctl * 10 > len(d)

def record_length(d, rec_min=3):
    """HCM's record detector: a byte value seen at equal spacing three times in
    a row votes for that spacing; returns the spacing with enough votes (0 if none)."""
    lp = [[-1, -1, -1] for _ in range(256)]
    cand = cnt = R = 0
    for p, c in enumerate(d):
        l = lp[c]
        d1 = p - l[0] if l[0] >= 0 else 0
        d2 = l[0] - l[1] if l[1] >= 0 else -1
        d3 = l[1] - l[2] if l[2] >= 0 else -2
        if d1 > 1 and d1 == d2 == d3 and d1 < 65536:
            if d1 == cand: cnt = min(cnt + 1, 64)
            elif cnt > 0: cnt -= 1
            else: cand, cnt = d1, 1
            if cnt >= rec_min and cand != R: R = cand
        l[2], l[1], l[0] = l[1], l[0], p
    return R

def comp(data, lc, lp, pb, pre=None, depth=512, nice=273):
    f = [] if pre is None else [pre]
    f.append({"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": max(4096, len(data)),
              "lc": lc, "lp": lp, "pb": pb, "nice_len": nice, "depth": depth, "mf": lzma.MF_BT4})
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32, filters=f)

def best_xz(data):
    base = lzma.compress(data, format=lzma.FORMAT_XZ, preset=9 | lzma.PRESET_EXTREME)   # = xz -9e
    cands = []
    for lc, lp, pb in itertools.product(range(5), range(3), range(3)):
        if lc + lp <= 4:
            cands.append((len(comp(data, lc, lp, pb)), (lc, lp, pb)))
    cands.sort()
    pres = [None]
    if looks_binary(data):
        dists = {1, 2, 4, 8, 12, 13, 16, 26, 48, 216}
        r = record_length(data)
        if 0 < r <= 256: dists.add(r)
        pres += [{"id": lzma.FILTER_DELTA, "dist": d} for d in sorted(dists)]
        pres += [{"id": i} for i in (lzma.FILTER_X86, lzma.FILTER_SPARC, lzma.FILTER_POWERPC,
                                      lzma.FILTER_ARM, lzma.FILTER_ARMTHUMB, lzma.FILTER_IA64)]
    best = base
    for _, (lc, lp, pb) in cands[:4]:
        for pre in pres:
            for depth in (512, 0, 4000):
                for nice in (273, 128):
                    try: blob = comp(data, lc, lp, pb, pre, depth, nice)
                    except lzma.LZMAError: continue
                    if len(blob) < len(best): best = blob
    return base, best

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src + '.xz'
    data = open(src, 'rb').read()
    base, best = best_xz(data)
    assert lzma.decompress(best) == data
    open(dst, 'wb').write(best)
    print('%s -> %s  %d bytes (xz -9e: %d bytes)' % (src, dst, len(best), len(base)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
