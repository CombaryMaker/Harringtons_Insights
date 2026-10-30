"""Search orderings of today's HCM stages (substitution method 1/2, HaBiTS menu pass, Juggle) with beam
search, score by final bits including every header, then verify finalists decode and run them through
the real coders (HCM per-context Huffman + HaBiTS trit blocks, xz, ZPAQ -m5)."""
import sys, math, re, itertools, time, json
import numpy as np
from scipy.special import gammaln
SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0, SCR)
exec(open(SCR + '/menu_search.py').read().split("mode = sys.argv[1]")[0])   # HaBiTS menu functions
import juggle as JG
import hcm

# ---------------- substitution (method 1 = top-k swap, method 2 = full table) ----------------
def best_code_order(m):
    codes = list(range(2 ** m))
    if m == 4:
        pref = [0b1111, 0b1110, 0b1011, 0b1101, 0b0111]
        return pref + sorted([c for c in codes if c not in pref], key=lambda v: (-bin(v).count('1'), -v))
    return sorted(codes, key=lambda v: (-bin(v).count('1'), -v))

def subst_perm(freq, m, k):
    N = 2 ** m
    F = [int(x) for x in np.argsort(-freq, kind='stable')[:k]]
    B = best_code_order(m)[:k]
    pi = dict(zip(F, B)); Bset = set(B)
    R = [s for s in range(N) if s not in pi]
    C = [c for c in range(N) if c not in Bset]
    Cset = set(C)
    fixed = [s for s in R if s in Cset]
    for s in fixed:
        pi[s] = s
    fset = set(fixed)
    for s, c in zip([s for s in R if s not in Cset], [c for c in C if c not in fset]):
        pi[s] = c
    return np.array([pi[s] for s in range(N)], dtype=np.int64), F

def table_cost(m, k):
    N = 2 ** m
    return sum(math.ceil(math.log2(N - i)) for i in range(k) if N - i > 1)

def blocks_of(bits, m):
    L = len(bits) // m * m
    w = (1 << np.arange(m - 1, -1, -1)).astype(np.int64)
    return bits[:L].reshape(-1, m).astype(np.int64) @ w, bits[L:]

def unblocks(vals, m, rest):
    out = ((vals[:, None] >> np.arange(m - 1, -1, -1)) & 1).astype(np.uint8).ravel()
    return np.concatenate((out, rest))

def S_apply(bits, m, k):
    v, rest = blocks_of(bits, m)
    pi, F = subst_perm(np.bincount(v, minlength=2 ** m), m, k)
    return unblocks(pi[v], m, rest), table_cost(m, k) + 4, ('S', m, k, F)

def S_inverse(bits, m, k, F):
    N = 2 ** m
    # rebuild the same permutation from the recorded top-k list
    freq = np.zeros(N);
    for r, f in enumerate(F):
        freq[f] = N * 4 - r
    pi, _ = subst_perm(freq, m, k)
    inv = np.argsort(pi)
    v, rest = blocks_of(bits, m)
    return unblocks(inv[v], m, rest)

# ---------------- ternary swap: HaBiTS parse -> swap m-trit blocks -> HaBiTS back to bits ----------------
# best trit codes = most 2s (trit 2 costs 1 bit, trits 0/1 cost 2 bits)
def best_trit_order(m):
    def digs(v):
        return [(v // 3 ** j) % 3 for j in range(m)]
    return sorted(range(3 ** m), key=lambda v: (-digs(v).count(2), v))

def tsubst_perm(freq, m, k):
    N = 3 ** m
    F = [int(x) for x in np.argsort(-freq, kind='stable')[:k]]
    B = best_trit_order(m)[:k]
    pi = dict(zip(F, B)); Bset = set(B)
    R = [s for s in range(N) if s not in pi]
    C = [c for c in range(N) if c not in Bset]; Cset = set(C)
    fixed = [s for s in R if s in Cset]
    for s in fixed:
        pi[s] = s
    fset = set(fixed)
    for s, c in zip([s for s in R if s not in Cset], [c for c in C if c not in fset]):
        pi[s] = c
    return np.array([pi[s] for s in range(N)], dtype=np.int64), F

def trit_blocks(t, m):
    L = len(t) // m * m
    w = (3 ** np.arange(m)).astype(np.int64)
    return t[:L].reshape(-1, m).astype(np.int64) @ w, t[L:]

def trit_unblocks(v, m, rest):
    out = ((v[:, None] // (3 ** np.arange(m))) % 3).astype(np.uint8).ravel()
    return np.concatenate((out, rest.astype(np.uint8)))

# HaBiTS trit ids from runs_parse: 0='0'(trit 2), 1='10'(trit 0), 2='11'(trit 1); convert to trit values
ID2T = np.array([2, 0, 1], dtype=np.uint8); T2ID = np.array([1, 2, 0], dtype=np.uint8)

def TS_apply(bits, m, k):
    best = None
    for cin in (0, 1):
        ids, tail = runs_parse((1 - bits) if cin else bits)
        t = ID2T[ids]
        v, rest = trit_blocks(t, m)
        pi, F = tsubst_perm(np.bincount(v, minlength=3 ** m), m, k)
        t2 = trit_unblocks(pi[v], m, rest)
        y = encode_blocks(T2ID[t2], (0, 1, 2), tail)
        if best is None or len(y) < len(best[0]):
            best = (y, cin, F)
    N = 3 ** m
    hdr = sum(math.ceil(math.log2(N - i)) for i in range(k) if N - i > 1) + 5
    return best[0], hdr, ('T', m, k, best[2], best[1])

def TS_inverse(bits, m, k, F, cin):
    N = 3 ** m
    freq = np.zeros(N)
    for r, f in enumerate(F):
        freq[f] = N * 4 - r
    pi, _ = tsubst_perm(freq, m, k); inv = np.argsort(pi)
    ids, tail = runs_parse(bits)
    t = ID2T[ids]
    v, rest = trit_blocks(t, m)
    x = encode_blocks(T2ID[trit_unblocks(inv[v], m, rest)], (0, 1, 2), tail)
    return (1 - x) if cin else x

# ---------------- HaBiTS menu: one pass, best of the 12 distinct remaps ----------------
def H_apply(bits):
    bk = None
    for t, y in candidates(bits):
        key = (min(len(y), best_len(y)), len(y))
        if bk is None or key < bk[0]:
            bk = (key, t, y)
    return bk[2], math.log2(12) + 1, ('H', bk[1])

def H_inverse(bits, t):
    return inverse(bits, [t])

# ---------------- Juggle: best trigger (lengths 1-6), optionally a second prefix-free trigger --------
T1 = [''.join(p) for L in range(1, 7) for p in itertools.product('01', repeat=L)]
T2 = [''.join(p) for L in range(2, 6) for p in itertools.product('01', repeat=L)]

def fj(bits, s, ts):
    pat = '|'.join(sorted(ts, key=len))
    ends = np.array([mm.end() for mm in re.finditer(pat, s)], dtype=np.int64)
    line = np.searchsorted(ends, np.arange(len(bits)), side='right'); odd = line % 2 == 0
    return np.concatenate((bits[odd], bits[~odd][::-1]))

def J_apply(bits, ntrig, score):
    s = ''.join(map(str, bits.tolist()))
    best = None
    for t in T1:
        y = fj(bits, s, [t]); c = score(y)
        if best is None or c < best[0]:
            best = (c, [t], y)
    if ntrig == 2:
        t0 = best[1][0]
        for t in T2:
            ts = [t0, t]
            if t == t0 or t.startswith(t0) or t0.startswith(t):
                continue
            y = fj(bits, s, ts); c = score(y)
            if c < best[0]:
                best = (c, ts, y)
    hdr = sum(3 + len(t) for t in best[1]) + 2
    return best[2], hdr, ('J', best[1])

def J_inverse(bits, ts):
    return JG.unjuggle(bits, ts).astype(np.uint8)

# ---------------- cheap scores ----------------
def kt_vec(z, o):
    return (-(gammaln(z + .5) + gammaln(o + .5) - math.log(math.pi) - gammaln(z + o + 1)) / math.log(2)).sum()

def K0(bits):   # balance-only: min(whole-stream, 8192-bit segments), adaptive
    n = len(bits); o = bits.sum()
    whole = kt_vec(np.array([n - o], float), np.array([o], float))
    seg = 8192; L = n // seg * seg
    if L:
        so = bits[:L].reshape(-1, seg).sum(1).astype(float); sz = seg - so
        r = bits[L:]; segc = kt_vec(sz, so) + kt_vec(np.array([len(r) - r.sum()], float), np.array([r.sum()], float))
    else:
        segc = whole
    return min(whole, segc) + 1

def KB(bits, k=16):   # balance per context of the previous 16 bits
    return JG.ctx_cost(bits, k)

# ---------------- beam search ----------------
STAGES = [('S', 4, 4), ('S', 4, 8), ('S', 4, 16), ('S', 8, 4), ('S', 8, 8), ('S', 8, 16), ('S', 8, 32),
          ('S', 8, 64), ('S', 8, 256), ('H',), ('J', 1), ('J', 2),
          ('T', 2, 3), ('T', 2, 9), ('T', 3, 9), ('T', 3, 27), ('T', 4, 16), ('T', 4, 81), ('T', 5, 32), ('T', 5, 243)]

def apply(stage, bits, score):
    if stage[0] == 'T':
        return TS_apply(bits, stage[1], stage[2])
    if stage[0] == 'S':
        return S_apply(bits, stage[1], stage[2])
    if stage[0] == 'H':
        return H_apply(bits)
    return J_apply(bits, stage[1], K0)   # trigger chosen for polarization; overall pipeline still scored by `score`

def undo(bits, rec):
    if rec[0] == 'T':
        return TS_inverse(bits, rec[1], rec[2], rec[3], rec[4])
    if rec[0] == 'S':
        return S_inverse(bits, rec[1], rec[2], rec[3])
    if rec[0] == 'H':
        return H_inverse(bits, rec[1])
    return J_inverse(bits, rec[1])

def name(rec):
    if rec[0] == 'T':
        return f"TriSwap{rec[1]}trit-{'full' if rec[2] == 3 ** rec[1] else 'top' + str(rec[2])}"
    if rec[0] == 'S':
        return f"Sub{rec[1]}bit-{'full' if rec[2] == 2 ** rec[1] else 'top' + str(rec[2])}"
    if rec[0] == 'H':
        return 'HaBiTS'
    return 'Juggle[' + ','.join(rec[1]) + ']'

def beam(bits, score, width=5, depth=4):
    start = (score(bits) + 32, 0.0, [], bits)
    beamset = [start]; allstates = [start]; seen = {bits.tobytes()}
    for d in range(depth):
        kids = []
        for tot, hdr, hist, b in beamset:
            for st in STAGES:
                y, h, rec = apply(st, b, score)
                key = y.tobytes()
                if key in seen:
                    continue
                seen.add(key)
                kids.append((score(y) + hdr + h + 32, hdr + h, hist + [rec], y))
        kids.sort(key=lambda x: x[0])
        beamset = kids[:width]; allstates += beamset
        print(f"   depth {d + 1}: best {beamset[0][0]:.0f} bits via {' > '.join(map(name, beamset[0][2]))}", flush=True)
    allstates.sort(key=lambda x: x[0])
    return allstates

def real_coders(bits, hdr):
    data = np.packbits(bits).tobytes()
    best_hcm = None
    for order in (2, 3, 4):
        b1 = hcm.stage1_encode(data, order)
        b2 = min((hcm.stage2_encode(b1, m) for m in (4, 5)), key=len)
        c = min(len(b1), len(b2)) + 1
        best_hcm = c if best_hcm is None else min(best_hcm, c)
    import lzma
    xzb = len(lzma.compress(data, preset=9 | lzma.PRESET_EXTREME)) * 8
    zp = JG.zpaq_bits(bits, 'p' + str(abs(hash(data)) % 10 ** 8))
    extra = hdr + 32 + 3   # pipeline headers + bit length + pad
    return best_hcm + extra, xzb + extra, zp + extra

if __name__ == '__main__':
    label, path, start, size = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    data = open(path, 'rb').read()[start:start + size]
    bits = np.unpackbits(np.frombuffer(data, np.uint8)); n = len(bits)
    per = lambda b: f"{round(b / n * 1_000_000):,}"
    finalists = {}
    for sname, sc in (('balance-only', K0), ('bit-context', KB)):
        t0 = time.time(); print(f"[{label}] beam scored by {sname} coding", flush=True)
        states = beam(bits, sc, width=8)
        print(f"   {sname} best: {per(states[0][0])} bits via {' > '.join(map(name, states[0][2])) or '(nothing)'} "
              f"| no-transform {per(sc(bits) + 32)} | {time.time() - t0:.0f}s", flush=True)
        for tot, hdr, hist, b in states[:4]:
            finalists[tuple(map(name, hist))] = (hdr, hist, b, sname, tot)
    finalists.setdefault((), (0.0, [], bits, 'none', 0))
    print(f"[{label}] finalists through real coders, 1,000,000 bits down to:", flush=True)
    for key, (hdr, hist, b, sname, tot) in finalists.items():
        y = b
        for rec in reversed(hist):
            y = undo(y, rec)
        ok = np.array_equal(y, bits)
        h, x, z = real_coders(b, hdr)
        print(f"   {' > '.join(key) or '(no transform)':55s} HCM {per(h):>9}  xz {per(x):>9}  ZPAQ {per(z):>9}  lossless {ok}", flush=True)
