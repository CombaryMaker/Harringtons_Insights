"""Harrington Juggle as described: scan for trigger(s) (prefix-free set), each line ends at a trigger,
odd lines concatenated to the front, even lines concatenated then reversed to the back.
Decoder: read odd lines forward from the front, even lines backward from the end, until pointers meet."""
import numpy as np, math, lzma, subprocess, os, sys, itertools
from math import lgamma

SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'


def cut_points(bits, triggers):
    """Return line end positions (exclusive) for greedy left-to-right trigger scanning."""
    n = len(bits)
    s = ''.join('1' if b else '0' for b in bits) if not isinstance(bits, str) else bits
    ends = []; i = 0
    tset = {}
    for t in triggers:
        tset.setdefault(len(t), set()).add(t)
    lens = sorted(tset)
    while i < n:
        hit = 0
        for L in lens:
            if s.startswith(tuple(tset[L]), i):
                hit = L; break
        if hit:
            i += hit; ends.append(i)
        else:
            i += 1
    return ends


def juggle(bits, triggers):
    ends = cut_points(bits, triggers)
    starts = [0] + ends
    lines = [bits[a:b] for a, b in zip(starts, ends + [len(bits)]) if b > a] if ends and ends[-1] == len(bits) else \
            [bits[a:b] for a, b in zip(starts, ends + [len(bits)])]
    lines = [l for l in lines if len(l)]
    odds = np.concatenate([l for k, l in enumerate(lines) if k % 2 == 0]) if lines else bits[:0]
    ev = [l for k, l in enumerate(lines) if k % 2 == 1]
    evens = np.concatenate(ev)[::-1] if ev else bits[:0]
    return np.concatenate((odds, evens)), len(odds)


def find_line(s, lo, hi, tset, lens, forward):
    """Scan a line in forward bit order. forward=True reads s[lo:hi] left->right; else reads right->left
    from hi-1 downward (which yields even lines in original order). Returns bits consumed."""
    k = 0; total = hi - lo
    if forward:
        while k < total:
            for L in lens:
                if k + L <= total and s[lo + k: lo + k + L] in tset[L]:
                    return k + L
            k += 1
        return total
    else:
        while k < total:
            for L in lens:
                if k + L <= total:
                    seg = s[hi - k - L: hi - k][::-1]
                    if seg in tset[L]:
                        return k + L
            k += 1
        return total


def unjuggle(out, triggers):
    s = ''.join('1' if b else '0' for b in out)
    tset = {}
    for t in triggers:
        tset.setdefault(len(t), set()).add(t)
    lens = sorted(tset)
    lo, hi = 0, len(s); res = []; odd = True
    while lo < hi:
        if odd:
            k = find_line(s, lo, hi, tset, lens, True); res.append(s[lo:lo + k]); lo += k
        else:
            k = find_line(s, lo, hi, tset, lens, False); res.append(s[hi - k:hi][::-1]); hi -= k
        odd = not odd
    return np.frombuffer(''.join(res).encode(), np.uint8) - 48


def kt(bits):
    o = int(bits.sum()); t = len(bits); z = t - o
    if t == 0:
        return 0.0
    return -(lgamma(z + .5) + lgamma(o + .5) - math.log(math.pi) - lgamma(t + 1)) / math.log(2)


def ctx_cost(bits, k=16):
    """Ideal adaptive cost with each bit predicted from the previous k bits (a bit-level context model)."""
    a = bits.astype(np.int64); n = len(a)
    ctx = np.zeros(n, dtype=np.int64)
    for j in range(1, k + 1):
        sh = np.zeros(n, dtype=np.int64); sh[j:] = a[:-j]; ctx = (ctx << 1) | sh
    keys = ctx * 2 + a
    u, c = np.unique(keys, return_counts=True)
    cx = u >> 1; bit = u & 1
    uc, inv = np.unique(cx, return_inverse=True)
    one = np.zeros(len(uc)); zero = np.zeros(len(uc))
    np.add.at(one, inv[bit == 1], c[bit == 1]); np.add.at(zero, inv[bit == 0], c[bit == 0])
    from scipy.special import gammaln
    return float((-(gammaln(zero + .5) + gammaln(one + .5) - math.log(math.pi) - gammaln(zero + one + 1)) / math.log(2)).sum())


def zpaq_bits(bits, tag):
    d = os.path.join(SCR, 'zj'); os.makedirs(d, exist_ok=True)
    f = os.path.join(d, tag + '.bin'); a = os.path.join(d, tag + '.zpaq')
    open(f, 'wb').write(np.packbits(bits).tobytes())
    if os.path.exists(a):
        os.remove(a)
    subprocess.run([os.path.join(SCR, 'zpaqbin'), 'a', a, f, '-m5'], capture_output=True, cwd=d)
    return os.path.getsize(a) * 8


def xz_bits(bits):
    return len(lzma.compress(np.packbits(bits).tobytes(), preset=9 | lzma.PRESET_EXTREME)) * 8
