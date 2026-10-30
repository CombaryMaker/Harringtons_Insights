"""HCM context-balance coder: per-context 0/1 balances (orders 0-4, 6, word, long match) blended by an
online mixer, refined by a secondary estimate, then real arithmetic coding. Encoder and decoder are
separate passes over the same model; the decoder only sees the file."""
import sys, math, time, os, subprocess, lzma, bz2
from array import array
sys.path.insert(0, '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad')
from real_files import BW, BR, AE, AD

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

class Model:
    def __init__(self, lr=0.02):
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
        if self.mexp >= 0:
            self.mk = min(self.mlen, 63); pc = self.mm[self.mk] >> 4
            st.append(ST[pc] if self.mexp else -ST[pc])
        else:
            st.append(0.0)
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
        p = int((self.pm + 3 * pa) / 4)
        return 1 if p < 1 else (4095 if p > 4095 else p)
    def update(self, y):
        err = (y - self.pm / 4096.0) * self.lr; ws = self.wsel; st = self.st
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
                    if cand > 0: self.mptr = cand; self.mlen = 1
                self.mtab[hh] = n
            self._ctx_byte()

def encode(data, lr):
    w = BW(); w.put(len(data), 32); e = AE(w); m = Model(lr)
    for byte in data:
        for k in range(7, -1, -1):
            y = (byte >> k) & 1; p = m.predict()
            if y: e.enc(0, p, 4096)
            else: e.enc(p, 4096, 4096)
            m.update(y)
    e.finish(); return w.bytes()

def decode(blob, lr):
    r = BR(blob); n = r.get(32); d = AD(r); m = Model(lr); out = bytearray()
    for _ in range(n):
        for k in range(8):
            p = m.predict(); t = d.target(4096)
            if t < p: d.consume(0, p, 4096); y = 1
            else: d.consume(p, 4096, 4096); y = 0
            m.update(y)
        out.append(m.buf[-1])
    return bytes(out)

if __name__ == '__main__':
    SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
    label = sys.argv[1]; lr = float(sys.argv[3]) if len(sys.argv) > 3 else 0.02
    if label == 'gatelog':
        data = subprocess.run(['node', SCR + '/gate_gen.js'], capture_output=True).stdout[:140000]
    else:
        data = open('/usr/lib/python3.12/pydoc_data/topics.py', 'rb').read()[:int(sys.argv[2])]
    t = time.perf_counter(); blob = encode(data, lr); te = time.perf_counter() - t
    fn = os.path.join(SCR, 'rf', f'{label}__context_balance.hcm'); os.makedirs(os.path.dirname(fn), exist_ok=True); open(fn, 'wb').write(blob)
    t = time.perf_counter(); back = decode(open(fn, 'rb').read(), lr); td = time.perf_counter() - t
    print(f"{label} ({len(data):,} bytes): HCM context-balance file {len(blob):,} bytes -> 1,000,000 bits down to {round(len(blob)/len(data)*1e6):,} "
          f"| enc {te:.1f} s dec {td:.1f} s | decodes exactly: {back == data} | lr {lr}", flush=True)
