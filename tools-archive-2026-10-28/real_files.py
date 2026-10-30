"""Real HCM Route 2 files: every back end writes an actual byte file, and a separate decoder rebuilds
the original from that file alone. Sizes reported are file lengths."""
import sys, os, time, math, lzma, bz2, subprocess
import numpy as np
SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0, SCR)
import route_test as RT
import hcm
import pipeline as PL

# ------------------------------------------------------------------ bit I/O
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

# ------------------------------------------------------------------ zero runs (symbols 256/257 = digits, 258 = end of run)
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
def zr_feed():
    """Incremental decoder: send tokens, get back ranks."""
    digits = []
    def feed(t, out):
        if t >= 256:
            if t == 258:
                run = int('1' + ''.join(digits), 2) - 1; out.extend([0] * run); digits.clear()
            else:
                digits.append(str(t - 256))
        else:
            out.append(t)
    return feed

# ------------------------------------------------------------------ arithmetic coder (Witten–Neal–Cleary, 32-bit)
TOP = (1 << 32) - 1; HALF = 1 << 31; Q1 = 1 << 30; Q3 = 3 << 30
class AE:
    def __init__(self, w): self.lo = 0; self.hi = TOP; self.pend = 0; self.w = w
    def _out(self, bit):
        self.w.b.append(bit); self.w.b.extend([1 - bit] * self.pend); self.pend = 0
    def enc(self, cl, ch, tot):
        rng = self.hi - self.lo + 1
        self.hi = self.lo + rng * ch // tot - 1; self.lo = self.lo + rng * cl // tot
        while True:
            if self.hi < HALF: self._out(0)
            elif self.lo >= HALF: self._out(1); self.lo -= HALF; self.hi -= HALF
            elif self.lo >= Q1 and self.hi < Q3: self.pend += 1; self.lo -= Q1; self.hi -= Q1
            else: break
            self.lo <<= 1; self.hi = (self.hi << 1) | 1
    def finish(self):
        self.pend += 1
        self._out(0 if self.lo < Q1 else 1)
class AD:
    def __init__(self, r): self.lo = 0; self.hi = TOP; self.r = r; self.v = r.get(32)
    def target(self, tot):
        rng = self.hi - self.lo + 1
        return ((self.v - self.lo + 1) * tot - 1) // rng
    def consume(self, cl, ch, tot):
        rng = self.hi - self.lo + 1
        self.hi = self.lo + rng * ch // tot - 1; self.lo = self.lo + rng * cl // tot
        while True:
            if self.hi < HALF: pass
            elif self.lo >= HALF: self.lo -= HALF; self.hi -= HALF; self.v -= HALF
            elif self.lo >= Q1 and self.hi < Q3: self.lo -= Q1; self.hi -= Q1; self.v -= Q1
            else: break
            self.lo <<= 1; self.hi = (self.hi << 1) | 1; self.v = (self.v << 1) | self.r.get()

class Freq:   # adaptive multi-symbol model with a Fenwick tree
    LIMIT = 1 << 16
    def __init__(self, n, inc=24):
        self.n = n; self.inc = inc; self.f = [1] * n; self.tree = [0] * (n + 1); self.tot = 0
        self._rebuild()
    def _rebuild(self):
        self.tree = [0] * (self.n + 1); self.tot = 0
        for i, c in enumerate(self.f): self._add(i, c)
    def _add(self, i, c):
        self.tot += c; i += 1
        while i <= self.n: self.tree[i] += c; i += i & -i
    def cum(self, i):  # sum of f[0..i-1]
        s = 0
        while i > 0: s += self.tree[i]; i -= i & -i
        return s
    def find(self, t):  # symbol whose range contains t
        pos = 0; step = 1 << self.n.bit_length()
        while step:
            nxt = pos + step
            if nxt <= self.n and self.tree[nxt] <= t: pos = nxt; t -= self.tree[nxt]
            step >>= 1
        return pos
    def update(self, s):
        self.f[s] += self.inc; self._add(s, self.inc)
        if self.tot > self.LIMIT:
            self.f = [max(1, c // 2) for c in self.f]; self._rebuild()

def ac_tokens_encode(tokens, ctx_order):
    w = BW(); e = AE(w); models = {}; prev = 0
    for t in tokens:
        key = prev if ctx_order else 0
        m = models.get(key) or models.setdefault(key, Freq(259))
        cl = m.cum(t); e.enc(cl, cl + m.f[t], m.tot); m.update(t); prev = t
    e.finish(); return w.b
def ac_tokens_decode(r, nranks, ctx_order):
    d = AD(r); models = {}; prev = 0; ranks = []; feed = zr_feed()
    while len(ranks) < nranks:
        key = prev if ctx_order else 0
        m = models.get(key) or models.setdefault(key, Freq(259))
        t = m.find(d.target(m.tot)); cl = m.cum(t); d.consume(cl, cl + m.f[t], m.tot); m.update(t)
        feed(t, ranks); prev = t
    return ranks

def ac_bits_encode(bits, seg):   # binary adaptive coder (KT-style counts), optional reset every `seg` bits
    w = BW(); e = AE(w); c0 = c1 = 1
    for i, b in enumerate(bits):
        if seg and i % seg == 0: c0 = c1 = 1
        tot = c0 + c1
        if b: e.enc(c0, tot, tot); c1 += 2
        else: e.enc(0, c0, tot); c0 += 2
        if c0 + c1 > 1 << 16: c0 = (c0 + 1) // 2; c1 = (c1 + 1) // 2
    e.finish(); return w.b
def ac_bits_decode(r, n, seg):
    d = AD(r); c0 = c1 = 1; out = np.zeros(n, np.uint8)
    for i in range(n):
        if seg and i % seg == 0: c0 = c1 = 1
        tot = c0 + c1; t = d.target(tot)
        if t >= c0: d.consume(c0, tot, tot); out[i] = 1; c1 += 2
        else: d.consume(0, c0, tot); c0 += 2
        if c0 + c1 > 1 << 16: c0 = (c0 + 1) // 2; c1 = (c1 + 1) // 2
    return out

# ------------------------------------------------------------------ Huffman with serialized table
def huff_table(tokens, nsym):
    from collections import Counter
    c = Counter(tokens); L = hcm.huff_lengths(c) if len(c) > 1 else {next(iter(c)): 1}
    assert max(L.values()) <= 31, "code length too long for 5-bit table"
    return L
def write_lengths(w, L, nsym):
    for s in range(nsym): w.put(L.get(s, 0), 5)
def read_lengths(r, nsym):
    L = {}
    for s in range(nsym):
        l = r.get(5)
        if l: L[s] = l
    return L

# ------------------------------------------------------------------ front end
def front(data):
    L, primary = RT.bwt(data); return RT.mtf(L), primary
def back(ranks, primary):
    return RT.ibwt(RT.imtf(bytes(ranks)), primary)

# ------------------------------------------------------------------ routes (each: encode -> bytes, decode(bytes) -> original)
def enc_huff(data, habits):
    ranks, primary = front(data); toks = zr_encode(ranks); L = huff_table(toks, 259); enc, _ = hcm.canonical(L)
    hb = []
    for t in toks:
        code, l = enc[t]; hb.extend((code >> k) & 1 for k in range(l - 1, -1, -1))
    w = BW(); w.put(len(data), 32); w.put(primary, 32); write_lengths(w, L, 259)
    if habits:
        s4, s5 = hcm.stage2_encode(hb, 4), hcm.stage2_encode(hb, 5)
        m, s2 = (4, s4) if len(s4) < len(s5) else (5, s5)
        use = len(s2) < len(hb)
        w.put(1 if use else 0, 1)
        if use: w.put(m - 4, 1); w.b.extend(s2)
        else: w.b.extend(hb)
    else:
        w.b.extend(hb)
    return w.bytes()
def dec_huff(blob, habits):
    r = BR(blob); n = r.get(32); primary = r.get(32); L = read_lengths(r, 259); _, dec = hcm.canonical(L)
    if habits and r.get(1):
        m = 4 + r.get(1); hb = hcm.stage2_decode(r.b[r.p:], m); r = BR(b''); r.b = hb; r.p = 0
    ranks = []; feed = zr_feed()
    while len(ranks) < n:
        l = 0; code = 0
        while (l, code) not in dec: code = (code << 1) | r.get(); l += 1
        feed(dec[(l, code)], ranks)
    return back(ranks, primary)

def enc_ac(data, ctx):
    ranks, primary = front(data); toks = zr_encode(ranks)
    w = BW(); w.put(len(data), 32); w.put(primary, 32); w.b.extend(ac_tokens_encode(toks, ctx)); return w.bytes()
def dec_ac(blob, ctx):
    r = BR(blob); n = r.get(32); primary = r.get(32); return back(ac_tokens_decode(r, n, ctx), primary)

def enc_A(data):   # back end A: Sort8 full swap (shrinking-index table) + real binary arithmetic coder
    ranks, primary = front(data)
    bits = np.unpackbits(np.frombuffer(ranks, np.uint8))
    y, _, rec = PL.S_apply(bits, 8, 256); F = rec[3]
    best = None
    for seg in (0, 8192):
        p = ac_bits_encode(y.tolist(), seg)
        if best is None or len(p) < len(best[1]): best = (seg, p)
    w = BW(); w.put(len(data), 32); w.put(primary, 32)
    remaining = list(range(256))
    for i, f in enumerate(F):        # your shrinking-index table: index among symbols not yet listed
        N = 256 - i
        if N > 1: w.put(remaining.index(f), math.ceil(math.log2(N)))
        remaining.remove(f)
    w.put(1 if best[0] else 0, 1); w.b.extend(best[1])
    return w.bytes()
def dec_A(blob):
    r = BR(blob); n = r.get(32); primary = r.get(32)
    remaining = list(range(256)); F = []
    for i in range(256):
        N = 256 - i
        idx = r.get(math.ceil(math.log2(N))) if N > 1 else 0
        F.append(remaining.pop(idx))
    seg = 8192 if r.get(1) else 0
    y = ac_bits_decode(r, n * 8, seg)
    bits = PL.S_inverse(y, 8, 256, F)
    return back(np.packbits(bits).tobytes(), primary)

ROUTES = [
    ('Zero runs + Huffman', lambda d: enc_huff(d, False), lambda b: dec_huff(b, False)),
    ('Back end B: zero runs + Huffman + HaBiTS', lambda d: enc_huff(d, True), lambda b: dec_huff(b, True)),
    ('Zero runs + arithmetic coder', lambda d: enc_ac(d, 0), lambda b: dec_ac(b, 0)),
    ('Zero runs + context arithmetic coder', lambda d: enc_ac(d, 1), lambda b: dec_ac(b, 1)),
    ('Back end A: Sort8 full + arithmetic coder', enc_A, dec_A),
]

def zpaq_size(data, tag):
    d = os.path.join(SCR, 'rf'); os.makedirs(d, exist_ok=True)
    f = os.path.join(d, tag + '.bin'); a = f + '.zpaq'; open(f, 'wb').write(data)
    if os.path.exists(a): os.remove(a)
    subprocess.run([os.path.join(SCR, 'zpaqbin'), 'a', a, f, '-m5'], capture_output=True, cwd=d)
    return os.path.getsize(a)

if __name__ == '__main__':
    if sys.argv[1] == 'edge':
        cases = [[0], [0, 0], [0, 0, 0], [0] * 255, [0] * 256, [0] * 65535, [0, 5, 0], [7] + [0] * 3,
                 [0] * 4 + [1, 2] + [0] * 9, [3, 0, 0, 0, 4], [9]]
        bad = 0
        for c in cases:
            out = []; feed = zr_feed()
            for t in zr_encode(c): feed(t, out)
            bad += out != c
        print("zero-run edge cases (runs of 1,2,3,255,256,65,535; streams starting/ending in zeros):", len(cases) - bad, "of", len(cases), "decode exactly")
        sys.exit()
    label = sys.argv[1]
    data = open(sys.argv[2], 'rb').read()[:int(sys.argv[3])] if label != 'gatelog' else \
        subprocess.run(['node', os.path.join(SCR, 'gate_gen.js')], capture_output=True).stdout[:140000]
    n = len(data); per = lambda nbytes: f"{round(nbytes * 8 / (n * 8) * 1e6):,}"
    out_dir = os.path.join(SCR, 'rf'); os.makedirs(out_dir, exist_ok=True)
    print(f"===== {label}: {n:,} bytes. Real file sizes; 1,000,000 bits down to =====", flush=True)
    t = time.perf_counter(); x = lzma.compress(data, preset=9 | lzma.PRESET_EXTREME); tx = time.perf_counter() - t
    t = time.perf_counter(); bz = bz2.compress(data, 9); tb = time.perf_counter() - t
    t = time.perf_counter(); zs = zpaq_size(data, label); tz = time.perf_counter() - t
    print(f"  {'xz -9e':45s} {len(x):>8,} bytes  {per(len(x)):>9}   {tx:6.2f} s", flush=True)
    print(f"  {'bzip2 -9':45s} {len(bz):>8,} bytes  {per(len(bz)):>9}   {tb:6.2f} s", flush=True)
    print(f"  {'ZPAQ -m5':45s} {zs:>8,} bytes  {per(zs):>9}   {tz:6.2f} s", flush=True)
    for name, enc, dec in ROUTES:
        t = time.perf_counter(); blob = enc(data); te = time.perf_counter() - t
        fn = os.path.join(out_dir, f"{label}__{name.split(':')[0].replace(' ', '_')}.hcm"); open(fn, 'wb').write(blob)
        t = time.perf_counter(); back_ = dec(open(fn, 'rb').read()); td = time.perf_counter() - t
        print(f"  {name:45s} {len(blob):>8,} bytes  {per(len(blob)):>9}   enc {te:5.1f} s  dec {td:5.1f} s  decodes exactly: {back_ == data}", flush=True)
