import sys, math, lzma, bz2, os, subprocess, time
import numpy as np
SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0, SCR)
import pipeline as PL      # Sort, K0 balance coder, KB bit-context coder
import hcm                 # Huffman helpers + HaBiTS trit-block stage

def zpaq_bytes(b, tag):
    d = os.path.join(SCR, 'rt'); os.makedirs(d, exist_ok=True)
    f = os.path.join(d, tag + '.bin'); a = f + '.zpaq'
    open(f, 'wb').write(b)
    if os.path.exists(a): os.remove(a)
    subprocess.run([os.path.join(SCR, 'zpaqbin'), 'a', a, f, '-m5'], capture_output=True, cwd=d)
    return os.path.getsize(a) * 8

# ---------------- route 2: dynamic-trigger Juggle = route each byte by the bytes before it ----------------
def bwt(data):
    a = np.frombuffer(data, np.uint8).astype(np.int64); n = len(a)
    rank = a.copy(); k = 1; idx = np.arange(n)
    while True:
        r2 = rank[(idx + k) % n]
        sa = np.lexsort((r2, rank))
        key = rank[sa] * (n + 1) + r2[sa]
        new = np.empty(n, np.int64); new[sa] = np.concatenate(([0], np.cumsum(key[1:] != key[:-1])))
        rank = new; k *= 2
        if rank.max() == n - 1 or k >= n: break
    L = a[(sa - 1) % n].astype(np.uint8)
    return L.tobytes(), int(np.flatnonzero(sa == 0)[0])

def ibwt(L, primary):
    Lb = np.frombuffer(L, np.uint8).astype(np.int64); n = len(Lb)
    C = np.concatenate(([0], np.cumsum(np.bincount(Lb, minlength=256))[:-1]))
    order = np.argsort(Lb, kind='stable'); lf = np.empty(n, np.int64); lf[order] = np.arange(n)
    out = bytearray(n); j = primary
    for i in range(n - 1, -1, -1):
        out[i] = L[j]; j = lf[j]
    return bytes(out)

def st1(data):   # route by the previous 1 byte, stable
    a = np.frombuffer(data, np.uint8)
    order = np.argsort(a[:-1], kind='stable') + 1
    return bytes([a[0]]) + a[order].tobytes(), a[-1]

def ist1(out, last):
    n = len(out); first = out[0]
    cnt = np.bincount(np.frombuffer(out, np.uint8), minlength=256).astype(np.int64)
    cnt[last] -= 1                       # bucket sizes = how often each byte precedes another
    starts = np.concatenate(([0], np.cumsum(cnt)[:-1])) + 1
    ptr = starts.copy(); res = bytearray(n); res[0] = first; prev = first
    for t in range(1, n):
        c = out[ptr[prev]]; ptr[prev] += 1; res[t] = c; prev = c
    return bytes(res)

def mtf(data):
    lst = list(range(256)); out = bytearray(len(data))
    for i, c in enumerate(data):
        r = lst.index(c); out[i] = r
        if r: lst.pop(r); lst.insert(0, c)
    return bytes(out)

def imtf(ranks):
    lst = list(range(256)); out = bytearray(len(ranks))
    for i, r in enumerate(ranks):
        c = lst[r]; out[i] = c
        if r: lst.pop(r); lst.insert(0, c)
    return bytes(out)

# ---------------- route 1: predictor -> rank of each byte among its guesses ----------------
class FreqList:
    __slots__ = ('syms', 'cnt', 'pos')
    def __init__(self): self.syms = []; self.cnt = {}; self.pos = {}
    def bump(self, s):
        if s not in self.cnt:
            self.cnt[s] = 0; self.pos[s] = len(self.syms); self.syms.append(s)
        self.cnt[s] += 1; i = self.pos[s]; c = self.cnt[s]
        while i > 0 and self.cnt[self.syms[i - 1]] <= c:   # most frequent first, most recent wins ties
            o = self.syms[i - 1]; self.syms[i - 1] = s; self.syms[i] = o; self.pos[o] = i; i -= 1
        self.pos[s] = i

def rank_transform(data, order, inverse=False):
    ctxs = {}; glob = FreqList(); glob.syms = list(range(256)); glob.cnt = {s: 0 for s in range(256)}; glob.pos = {s: s for s in range(256)}
    out = bytearray(len(data)); hist = b''
    for i in range(len(data)):
        key = bytes(hist[-order:]) if order else b''
        cl = ctxs.get(key)
        if cl is None: cl = ctxs[key] = FreqList()
        if not inverse:
            s = data[i]
            if s in cl.pos: r = cl.pos[s]
            else:
                r = len(cl.syms)
                for g in glob.syms:
                    if g == s: break
                    if g not in cl.pos: r += 1
            out[i] = r
        else:
            r = data[i]
            if r < len(cl.syms): s = cl.syms[r]
            else:
                k = r - len(cl.syms)
                for g in glob.syms:
                    if g in cl.pos: continue
                    if k == 0: s = g; break
                    k -= 1
            out[i] = s
        cl.bump(s); glob.bump(s)
        hist = (hist + bytes([s]))[-max(order, 1):]
    return bytes(out)

# ---------------- coders from today: Huffman (+ zero runs), HaBiTS trit blocks, Sort + balance coding ----------------
def huff_bits(symbols, alpha):
    from collections import Counter
    c = Counter(symbols); L = hcm.huff_lengths(c) if len(c) > 1 else {next(iter(c)): 1}
    enc, _ = hcm.canonical(L)
    payload = sum(c[s] * L[s] for s in c)
    return payload + alpha * 5, enc

def zero_runs(r):   # bzip2-style: each run of 0s written as bijective base-2 digits (symbols 256/257)
    out = []; i = 0; n = len(r)
    while i < n:
        if r[i] == 0:
            j = i
            while j < n and r[j] == 0: j += 1
            L = j - i + 1
            for b in bin(L)[3:]: out.append(256 + int(b))
            out.append(258)   # run terminator keeps it simple and decodable
            i = j
        else:
            out.append(r[i]); i += 1
    return out

def coders(stream, tag):
    res = {}
    h, enc = huff_bits(list(stream), 256); res['Huffman'] = h
    zr = zero_runs(stream); hz, encz = huff_bits(zr, 259); res['Zero runs + Huffman'] = hz
    bits = []
    for s in zr:
        code, l = encz[s]
        bits.extend((code >> k) & 1 for k in range(l - 1, -1, -1))
    tb = min(len(hcm.stage2_encode(bits, m)) for m in (4, 5))
    res['Zero runs + Huffman + HaBiTS 5-trit'] = min(hz, tb + 259 * 5)
    b = np.unpackbits(np.frombuffer(stream, np.uint8))
    y, hdr, _ = PL.S_apply(b, 8, 256)
    res['Sort8 full + balance only'] = PL.K0(y) + hdr
    res['Bit context (prev 16 bits)'] = PL.KB(b)
    res['xz on it'] = len(lzma.compress(stream, preset=9 | lzma.PRESET_EXTREME)) * 8
    res['bz2 on it'] = len(bz2.compress(stream, 9)) * 8
    res['ZPAQ on it'] = zpaq_bytes(stream, tag)
    return res

if __name__ == '__main__':
    label, path, start, size = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    data = open(path, 'rb').read()[start:start + size]
    if label == 'gatelog':
        import subprocess as sp
        data = sp.run(['node', '-e', open(os.path.join(SCR, 'gate_gen.js')).read()], capture_output=True).stdout[:size]
    n = len(data) * 8; P = lambda b: f"{round(b / n * 1e6):,}"
    print(f"===== {label} ({len(data):,} bytes), 1,000,000 bits down to =====", flush=True)
    print(f"  untouched file: xz {P(len(lzma.compress(data, preset=9|lzma.PRESET_EXTREME))*8)} | bz2 {P(len(bz2.compress(data,9))*8)} | ZPAQ -m5 {P(zpaq_bytes(data, label + '_orig'))}", flush=True)
    routes = []
    t0 = time.time(); L, pidx = bwt(data); assert ibwt(L, pidx) == data
    m = mtf(L); assert imtf(m) == L; routes.append(('Route 2: full-context Juggle (BWT) > MTF', m, 32))
    s1, last = st1(data); assert ist1(s1, last) == data
    m1 = mtf(s1); assert imtf(m1) == s1; routes.append(('Route 2: 1-byte-context Juggle > MTF', m1, 8))
    for order in (1, 2, 3):
        r = rank_transform(data, order); assert rank_transform(r, order, inverse=True) == data
        routes.append((f'Route 1: predictor ({order}-byte context) ranks', r, 2))
    for name, stream, extra in routes:
        z = np.frombuffer(stream, np.uint8); zeros = (z == 0).mean() * 100
        res = coders(stream, label + str(abs(hash(name)) % 10**6))
        best = min((v, k) for k, v in res.items() if not k.endswith('on it'))
        print(f"\n  {name}  [decodes exactly; {zeros:.1f}% of symbols are rank 0]", flush=True)
        for k, v in res.items():
            print(f"     {k:38s} {P(v + extra + 32):>9}{'   <- best HCM-side coder' if k == best[1] else ''}", flush=True)
