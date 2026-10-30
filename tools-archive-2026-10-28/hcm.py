"""HCM end-to-end test: adaptive per-context Huffman (learns counts as it goes, escape to shorter
contexts), then optional HaBiTS trit-block Huffman stage. Encodes, decodes, verifies, counts every bit."""
import heapq, sys, time, math
from collections import Counter

ESC = 256


def huff_lengths(counts):
    syms = list(counts)
    if len(syms) == 1:
        return {syms[0]: 0}
    h = [(counts[s], i, [s]) for i, s in enumerate(syms)]
    heapq.heapify(h); L = dict.fromkeys(syms, 0); k = len(h)
    while len(h) > 1:
        c1, _, a = heapq.heappop(h); c2, _, b = heapq.heappop(h)
        for s in a + b:
            L[s] += 1
        k += 1; heapq.heappush(h, (c1 + c2, k, a + b))
    return L


def canonical(L):
    enc = {}; dec = {}; code = 0; prev = 0
    for s in sorted(L, key=lambda s: (L[s], s)):
        l = L[s]; code <<= (l - prev); prev = l
        enc[s] = (code, l); dec[(l, code)] = s; code += 1
    return enc, dec


class Ctx:
    __slots__ = ('cnt', 'total', 'nxt', 'enc', 'dec')
    def __init__(self):
        self.cnt = {}; self.total = 0; self.nxt = 1; self.enc = {ESC: (0, 0)}; self.dec = {(0, 0): ESC}
    def update(self, s):
        self.cnt[s] = self.cnt.get(s, 0) + 1; self.total += 1
        if self.total >= self.nxt:
            self.nxt = self.total + max(1, self.total // 4)
            c = dict(self.cnt); c[ESC] = max(1, len(self.cnt))
            self.enc, self.dec = canonical(huff_lengths(c))


class BitW:
    def __init__(self): self.b = []
    def put(self, code, l):
        for i in range(l - 1, -1, -1):
            self.b.append((code >> i) & 1)


class BitR:
    def __init__(self, b): self.b = b; self.p = 0
    def get(self): v = self.b[self.p]; self.p += 1; return v


def model(order):
    return [dict() for _ in range(order + 1)]


def ctx_of(tabs, o, hist):
    key = bytes(hist[len(hist) - o:]) if o else b''
    t = tabs[o].get(key)
    if t is None:
        t = tabs[o][key] = Ctx()
    return t


def stage1_encode(data, order):
    tabs = model(order); w = BitW(); hist = []
    for s in data:
        chain = [ctx_of(tabs, o, hist) for o in range(min(order, len(hist)), -1, -1)]
        done = False
        for t in chain:
            if s in t.enc:
                w.put(*t.enc[s]); done = True; break
            w.put(*t.enc[ESC])
        if not done:
            w.put(s, 8)
        for t in chain:
            t.update(s)
        hist.append(s)
    return w.b


def stage1_decode(bits, n, order):
    tabs = model(order); r = BitR(bits); hist = []
    for _ in range(n):
        chain = [ctx_of(tabs, o, hist) for o in range(min(order, len(hist)), -1, -1)]
        s = None
        for t in chain:
            l = 0; code = 0
            while (l, code) not in t.dec:
                code = (code << 1) | r.get(); l += 1
            v = t.dec[(l, code)]
            if v != ESC:
                s = v; break
        if s is None:
            s = 0
            for _ in range(8):
                s = (s << 1) | r.get()
        for t in chain:
            t.update(s)
        hist.append(s)
    return bytes(hist)


# ---- HaBiTS stage: bits -> trits (0->2, 11->1, 10->0), static m-trit block Huffman with sent table
def habits_trits(bits):
    t = []; i = 0; n = len(bits)
    while i < n:
        if bits[i] == 0:
            t.append(2); i += 1
        elif i + 1 < n:
            t.append(1 if bits[i + 1] else 0); i += 2
        else:
            return t, 1
    return t, 0


def habits_bits(t, tail):
    out = []
    for x in t:
        out.extend((0,) if x == 2 else ((1, 1) if x == 1 else (1, 0)))
    if tail:
        out.append(1)
    return out


def stage2_encode(bits, m):
    t, tail = habits_trits(bits)
    nb = len(t) // m
    blocks = [sum(t[i * m + j] * 3 ** j for j in range(m)) for i in range(nb)]
    rest = t[nb * m:]
    c = Counter(blocks)
    L = huff_lengths(c) if len(c) > 1 else {next(iter(c)): 1}
    enc, _ = canonical(L)
    w = BitW()
    # header: trit count (32), tail (1), table = 5-bit length for each of 3^m symbols (0 = unused)
    w.put(len(t), 32); w.put(tail, 1)
    for s in range(3 ** m):
        w.put(L.get(s, 0), 5)
    for b in blocks:
        w.put(*enc[b])
    for x in rest:
        w.put(x, 2)
    return w.b


def stage2_decode(b, m):
    r = BitR(b)
    nt = 0
    for _ in range(32):
        nt = (nt << 1) | r.get()
    tail = r.get()
    L = {}
    for s in range(3 ** m):
        l = 0
        for _ in range(5):
            l = (l << 1) | r.get()
        if l:
            L[s] = l
    _, dec = canonical(L)
    t = []
    for _ in range(nt // m):
        l = 0; code = 0
        while (l, code) not in dec:
            code = (code << 1) | r.get(); l += 1
        v = dec[(l, code)]
        for j in range(m):
            t.append(v % 3); v //= 3
    for _ in range(nt - (nt // m) * m):
        t.append((r.get() << 1) | r.get())
    return habits_bits(t, tail)


if __name__ == '__main__':
    path, start, size = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    data = open(path, 'rb').read()[start:start + size]
    nbits = len(data) * 8
    per = lambda b: f"{round(b / nbits * 1_000_000):,}"
    for order in (2, 3, 4):
        t0 = time.time()
        b1 = stage1_encode(data, order)
        ok1 = stage1_decode(b1, len(data), order) == data
        line = f"order {order}: HCM stage 1 -> {per(len(b1) + 32)} bits (lossless {ok1})"
        best = None
        for m in (4, 5):
            b2 = stage2_encode(b1, m)
            ok2 = stage2_decode(b2, m) == b1
            if best is None or len(b2) < best[0]:
                best = (len(b2), m, ok2)
        line += f" | + HaBiTS {best[1]}-trit blocks -> {per(best[0] + 32)} bits (lossless {best[2]}) | {time.time() - t0:.0f}s"
        print(line, flush=True)
