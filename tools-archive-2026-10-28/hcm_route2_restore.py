"""HCM Route 2 restore point (Oct 24, 2026): BWT > move-to-front > zero-run tokens > arithmetic coder with
the previous token as context. Self-contained. compress(data) -> bytes, decompress(bytes) -> data.
Reference results: 200 KB Python docs text -> 35,378 bytes; 140 KB gate log -> 12,081 bytes."""
import numpy as np

# ---------------- bit I/O ----------------
class BW:
    def __init__(self): self.b = []
    def put(self, v, n):
        for k in range(n - 1, -1, -1): self.b.append((v >> k) & 1)
    def bytes(self):
        a = np.array(self.b + [0] * (-len(self.b) % 8), dtype=np.uint8)
        return np.packbits(a).tobytes()
class BR:
    def __init__(self, data): self.b = np.unpackbits(np.frombuffer(data, np.uint8)).tolist(); self.p = 0
    def get(self, n=1):
        v = 0
        for _ in range(n):
            v = (v << 1) | (self.b[self.p] if self.p < len(self.b) else 0); self.p += 1
        return v

# ---------------- stage 1: BWT (context sort) ----------------
def bwt(data):
    a = np.frombuffer(data, np.uint8).astype(np.int64); n = len(a)
    rank = a.copy(); k = 1; idx = np.arange(n)
    while True:
        r2 = rank[(idx + k) % n]; sa = np.lexsort((r2, rank))
        key = rank[sa] * (n + 1) + r2[sa]
        new = np.empty(n, np.int64); new[sa] = np.concatenate(([0], np.cumsum(key[1:] != key[:-1])))
        rank = new; k *= 2
        if rank.max() == n - 1 or k >= n: break
    return a[(sa - 1) % n].astype(np.uint8).tobytes(), int(np.flatnonzero(sa == 0)[0])
def ibwt(L, primary):
    Lb = np.frombuffer(L, np.uint8).astype(np.int64); n = len(Lb)
    order = np.argsort(Lb, kind='stable'); lf = np.empty(n, np.int64); lf[order] = np.arange(n)
    out = bytearray(n); j = primary
    for i in range(n - 1, -1, -1): out[i] = L[j]; j = lf[j]
    return bytes(out)

# ---------------- stage 2: move-to-front ----------------
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

# ---------------- stage 3: zero-run tokens (256/257 = digits, 258 = end of run) ----------------
def zr_encode(r):
    out = []; i = 0; n = len(r)
    while i < n:
        if r[i] == 0:
            j = i
            while j < n and r[j] == 0: j += 1
            for b in bin(j - i + 1)[3:]: out.append(256 + int(b))
            out.append(258); i = j
        else:
            out.append(r[i]); i += 1
    return out

# ---------------- stage 4: arithmetic coder, previous token as context ----------------
TOP = (1 << 32) - 1; HALF = 1 << 31; Q1 = 1 << 30; Q3 = 3 << 30
class AE:
    def __init__(self, w): self.lo = 0; self.hi = TOP; self.pend = 0; self.w = w
    def _out(self, bit): self.w.b.append(bit); self.w.b.extend([1 - bit] * self.pend); self.pend = 0
    def enc(self, cl, ch, tot):
        rng = self.hi - self.lo + 1
        self.hi = self.lo + rng * ch // tot - 1; self.lo = self.lo + rng * cl // tot
        while True:
            if self.hi < HALF: self._out(0)
            elif self.lo >= HALF: self._out(1); self.lo -= HALF; self.hi -= HALF
            elif self.lo >= Q1 and self.hi < Q3: self.pend += 1; self.lo -= Q1; self.hi -= Q1
            else: break
            self.lo <<= 1; self.hi = (self.hi << 1) | 1
    def finish(self): self.pend += 1; self._out(0 if self.lo < Q1 else 1)
class AD:
    def __init__(self, r): self.lo = 0; self.hi = TOP; self.r = r; self.v = r.get(32)
    def target(self, tot): return ((self.v - self.lo + 1) * tot - 1) // (self.hi - self.lo + 1)
    def consume(self, cl, ch, tot):
        rng = self.hi - self.lo + 1
        self.hi = self.lo + rng * ch // tot - 1; self.lo = self.lo + rng * cl // tot
        while True:
            if self.hi < HALF: pass
            elif self.lo >= HALF: self.lo -= HALF; self.hi -= HALF; self.v -= HALF
            elif self.lo >= Q1 and self.hi < Q3: self.lo -= Q1; self.hi -= Q1; self.v -= Q1
            else: break
            self.lo <<= 1; self.hi = (self.hi << 1) | 1; self.v = (self.v << 1) | self.r.get()
class Freq:
    LIMIT = 1 << 16
    def __init__(self, n, inc=24):
        self.n = n; self.inc = inc; self.f = [1] * n; self._rebuild()
    def _rebuild(self):
        self.tree = [0] * (self.n + 1); self.tot = 0
        for i, c in enumerate(self.f): self._add(i, c)
    def _add(self, i, c):
        self.tot += c; i += 1
        while i <= self.n: self.tree[i] += c; i += i & -i
    def cum(self, i):
        s = 0
        while i > 0: s += self.tree[i]; i -= i & -i
        return s
    def find(self, t):
        pos = 0; step = 1 << self.n.bit_length()
        while step:
            nxt = pos + step
            if nxt <= self.n and self.tree[nxt] <= t: pos = nxt; t -= self.tree[nxt]
            step >>= 1
        return pos
    def update(self, s):
        self.f[s] += self.inc; self._add(s, self.inc)
        if self.tot > self.LIMIT: self.f = [max(1, c // 2) for c in self.f]; self._rebuild()

def compress(data):
    L, primary = bwt(data); toks = zr_encode(mtf(L))
    w = BW(); w.put(len(data), 32); w.put(primary, 32); e = AE(w); models = {}; prev = 0
    for t in toks:
        m = models.get(prev) or models.setdefault(prev, Freq(259))
        cl = m.cum(t); e.enc(cl, cl + m.f[t], m.tot); m.update(t); prev = t
    e.finish(); return w.bytes()

def decompress(blob):
    r = BR(blob); n = r.get(32); primary = r.get(32); d = AD(r); models = {}; prev = 0
    ranks = []; digits = []
    while len(ranks) < n:
        m = models.get(prev) or models.setdefault(prev, Freq(259))
        t = m.find(d.target(m.tot)); cl = m.cum(t); d.consume(cl, cl + m.f[t], m.tot); m.update(t); prev = t
        if t == 258: ranks.extend([0] * (int('1' + ''.join(digits), 2) - 1)); digits.clear()
        elif t >= 256: digits.append(str(t - 256))
        else: ranks.append(t)
    return ibwt(imtf(bytes(ranks)), primary)

if __name__ == '__main__':
    import sys
    src = open(sys.argv[1], 'rb').read()[:int(sys.argv[2]) if len(sys.argv) > 2 else None]
    blob = compress(src)
    print(f"{len(src):,} bytes -> {len(blob):,} bytes; decodes exactly: {decompress(blob) == src}")
