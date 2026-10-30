"""HCM context-balance coder (Oct 24, 2026). Self-contained.

How it works, per bit:
  1. Each context (the last 1, 2, 3, 4 and 6 bytes, the current partial byte alone, and the current word)
     keeps a running 0/1 balance for "what bit comes next here", in a hashed table.
  2. A long-match finder looks for the last place the previous 6 bytes appeared and predicts the bit
     that followed there, with a learned confidence by match length.
  3. An online mixer blends all these balances (weights chosen by partial byte and match length) and
     learns from every bit; a secondary estimate (previous byte + partial byte) refines the result.
  4. A 32-bit arithmetic coder writes the bit using that probability.
The decoder runs the identical model, so it needs only the file: a 32-bit length, then the coded bits.

Reference results: 200 KB Python docs text -> 29,725 bytes; 140 KB gate log -> 8,447 bytes.
compress(data) -> bytes, decompress(blob) -> data
"""
import math
import numpy as np
from array import array

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

ST = [0.0] * 4096
for p in range(1, 4096): ST[p] = math.log(p / (4096 - p))
ST[0] = ST[1]
def squash(x):
    if x > 20: x = 20
    elif x < -20: x = -20
    return 4096.0 / (1.0 + math.exp(-x))
RT = [int(65536 / (n + 1.6)) for n in range(256)]
ORDERS = (1, 2, 3, 4, 6)
TBITS = 22; MASK = (1 << TBITS) - 1
LIMIT_LO, LIMIT_HI = 60, 255

CFG = dict(tbits=22, match=True, verify=False, word=True, apm=True, apm_w=0.75, learn=True, lr=0.008)
class Model:
    def __init__(self, lr=None):
        cfg = CFG; lr = cfg['lr'] if lr is None else lr; self.cfg = cfg
        global TBITS, MASK
        TBITS = cfg['tbits']; MASK = (1 << TBITS) - 1
        self.nm = len(ORDERS) + 2           # hashed orders + order-0 + word
        self.prob = [array('H', [32768]) * (1 << TBITS) for _ in range(self.nm)]
        self.cnt = [array('B', [0]) * (1 << TBITS) for _ in range(self.nm)]
        self.limits = [LIMIT_LO] + [LIMIT_HI] * (self.nm - 1)
        self.nin = self.nm + 2               # + match input + bias
        self.w = [[0.3] * self.nin for _ in range(256 * 4)]
        self.lr = lr
        self.apm = [[int(squash((j - 16) / 2.0) * 16) for j in range(33)] for _ in range(256 * 256)]
        self.buf = bytearray(); self.c0 = 1; self.bitpos = 0
        self.h = [0] * self.nm; self.word = 0
        self.mtab = array('i', [0]) * (1 << 20); self.mptr = 0; self.mlen = 0
        self.mm = array('H', [49152]) * 64
        self._ctx_byte()
    def _ctx_byte(self):
        b = self.buf; n = len(b); H = self.h
        for k, o in enumerate(ORDERS):
            x = (o + 1) * 0x9E3779B1
            for j in range(1, o + 1):
                x = ((x ^ (b[n - j] if n >= j else 0)) * 0x01000193 + j) & 0xFFFFFFFF
            H[k] = (x * 0x2545F491) & 0xFFFFFFFF
        H[len(ORDERS)] = 0                       # order 0
        H[len(ORDERS) + 1] = (self.word * 0x6F4F2A35 + 0x51ED27) & 0xFFFFFFFF
        self.last = b[-1] if n else 0
    def predict(self):
        c0 = self.c0; cm = (c0 * 0x9E3779B1) & 0xFFFFFFFF
        idx = self.idx = [((hh ^ cm) & MASK) for hh in self.h]
        idx[len(ORDERS)] = c0
        st = self.st = [ST[self.prob[i][idx[i]] >> 4] for i in range(self.nm)]
        # long-match input
        self.mexp = -1
        if self.mlen > 0 and self.mptr < len(self.buf):
            pb = self.buf[self.mptr]
            if ((pb | 256) >> (8 - self.bitpos)) == c0:
                self.mexp = (pb >> (7 - self.bitpos)) & 1
        if not self.cfg['match']: self.mexp = -1
        if self.mexp >= 0:
            self.mk = min(self.mlen, 63); pc = self.mm[self.mk] >> 4
            st.append(ST[pc] if self.mexp else -ST[pc])
        else:
            st.append(0.0)
        if not self.cfg['word']: st[len(ORDERS) + 1] = 0.0
        st.append(0.25)
        ms = 0 if self.mlen == 0 else (1 if self.mlen < 16 else (2 if self.mlen < 32 else 3))
        self.wsel = self.w[c0 + 256 * ms]; ws = self.wsel
        dot = 0.0
        for i in range(self.nin): dot += ws[i] * st[i]
        self.pm = squash(dot)
        # secondary estimate indexed by previous byte and partial byte, interpolated on stretch(p)
        s = max(-7.999, min(7.999, dot)); pos = (s + 8.0) * 2.0; j = int(pos); f = pos - j
        self.aj = j; self.af = f; self.arow = self.apm[c0 + 256 * self.last]
        pa = (self.arow[j] * (1 - f) + self.arow[j + 1] * f) / 16.0
        w = self.cfg['apm_w'] if self.cfg['apm'] else 0.0
        p = int(self.pm * (1 - w) + pa * w)
        return 1 if p < 1 else (4095 if p > 4095 else p)
    def update(self, y):
        err = (y - self.pm / 4096.0) * self.lr; ws = self.wsel; st = self.st
        if self.cfg['learn']:
            for i in range(self.nin): ws[i] += err * st[i]
        tgt = 65535 if y else 0
        for i in range(self.nm):
            P = self.prob[i]; C = self.cnt[i]; k = self.idx[i]; n = C[k]
            P[k] = P[k] + (((tgt - P[k]) * RT[n]) >> 16)
            if n < self.limits[i]: C[k] = n + 1
        if self.mexp >= 0:
            v = self.mm[self.mk]; self.mm[self.mk] = v + (((65535 if y == self.mexp else 0) - v) >> 5)
        row = self.arow; j = self.aj; t16 = 65535 if y else 0
        row[j] += int((t16 - row[j]) * (1 - self.af)) >> 6
        row[j + 1] += int((t16 - row[j + 1]) * self.af) >> 6
        self.c0 = (self.c0 << 1) | y; self.bitpos += 1
        if self.bitpos == 8:
            byte = self.c0 & 255; self.buf.append(byte); self.c0 = 1; self.bitpos = 0
            # word context: letters only, case folded
            if 65 <= byte <= 90 or 97 <= byte <= 122: self.word = ((self.word * 0x2C9277B5) ^ (byte | 32)) & 0xFFFFFFFF
            else: self.word = 0
            # long-match bookkeeping (hash of last 6 bytes -> next position)
            n = len(self.buf)
            if self.mlen > 0 and self.mptr < n - 1 and self.buf[self.mptr] == byte: self.mptr += 1; self.mlen += 1
            else: self.mlen = 0
            if n >= 6:
                b = self.buf; hh = 0
                for j2 in range(1, 7): hh = (hh * 0x2F0B4A3 + b[n - j2]) & 0xFFFFF
                if self.mlen == 0:
                    cand = self.mtab[hh]
                    if cand > 0:
                        if self.cfg['verify']:
                            L = 0
                            while L < 32 and cand - 1 - L >= 0 and b[cand - 1 - L] == b[n - 1 - L]: L += 1
                            if L >= 6: self.mptr = cand; self.mlen = L
                        else:
                            self.mptr = cand; self.mlen = 1
                self.mtab[hh] = n
            self._ctx_byte()

def compress(data, lr=None):
    w = BW(); w.put(len(data), 32); e = AE(w); m = Model(lr)
    for byte in data:
        for k in range(7, -1, -1):
            y = (byte >> k) & 1; p = m.predict()
            if y: e.enc(0, p, 4096)
            else: e.enc(p, 4096, 4096)
            m.update(y)
    e.finish(); return w.bytes()

def decompress(blob, lr=None):
    r = BR(blob); n = r.get(32); d = AD(r); m = Model(lr); out = bytearray()
    for _ in range(n):
        for k in range(8):
            p = m.predict(); t = d.target(4096)
            if t < p: d.consume(0, p, 4096); y = 1
            else: d.consume(p, 4096, 4096); y = 0
            m.update(y)
        out.append(m.buf[-1])
    return bytes(out)


