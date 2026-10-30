import numpy as np, lzma, bz2, math, itertools, time, sys

# Block ids: 0 = '0', 1 = '10', 2 = '11'  (parse code {0,10,11}; complement input for {1,01,00})
PERMS = list(itertools.permutations((0, 1, 2)))
LOG12 = math.log2(12)


def runs_parse(x):
    n = len(x)
    z = np.flatnonzero(x == 0)
    if len(z) == 0:
        return np.full(n // 2, 2, dtype=np.uint8), bool(n % 2)
    prev = np.empty_like(z); prev[0] = -1; prev[1:] = z[:-1]
    r = z - prev - 1
    seglen = (r >> 1) + 1
    ids = np.full(int(seglen.sum()), 2, dtype=np.uint8)
    ids[np.cumsum(seglen) - 1] = np.where(r & 1, 1, 0)
    rl = n - 1 - int(z[-1])
    if rl >= 2:
        ids = np.concatenate((ids, np.full(rl // 2, 2, dtype=np.uint8)))
    return ids, bool(rl & 1)


def encode_blocks(ids, perm, tail):
    if len(ids) == 0:
        return np.ones(1, dtype=np.uint8) if tail else np.zeros(0, dtype=np.uint8)
    oid = np.asarray(perm, dtype=np.uint8)[ids]
    lens = np.where(oid == 0, 1, 2).astype(np.int64)
    ends = np.cumsum(lens); starts = ends - lens
    out = np.empty(int(ends[-1]) + (1 if tail else 0), dtype=np.uint8)
    two = oid != 0
    out[starts] = two
    out[starts[two] + 1] = (oid[two] == 2)
    if tail:
        out[-1] = 1
    return out


def counts(x):
    n = len(x); z = np.flatnonzero(x == 0)
    if len(z) == 0:
        return 0, 0, n // 2, n & 1
    prev = np.empty_like(z); prev[0] = -1; prev[1:] = z[:-1]
    r = z - prev - 1
    odd = int((r & 1).sum())
    nL1 = int((r >> 1).sum())
    rl = n - 1 - int(z[-1])
    return len(r) - odd, odd, nL1 + (rl >> 1), rl & 1


def best_len(x):
    b = None
    for xi in (x, 1 - x):
        nS, nL0, nL1, t = counts(xi)
        L = 2 * (nS + nL0 + nL1) - max(nS, nL0, nL1) + t
        b = L if b is None else min(b, L)
    return b


def candidates(cur):
    for cin in (0, 1):
        xi = (1 - cur) if cin else cur
        ids, tail = runs_parse(xi)
        for perm in PERMS:
            yield (cin, perm), encode_blocks(ids, perm, tail)


def forward(x, hist):
    y = x
    for cin, perm in hist:
        ids, tail = runs_parse((1 - y) if cin else y)
        y = encode_blocks(ids, perm, tail)
    return y


def inverse(y, hist):
    for cin, perm in reversed(hist):
        ids, tail = runs_parse(y)
        inv = tuple(int(i) for i in np.argsort(perm))
        y = encode_blocks(ids, inv, tail)
        if cin:
            y = 1 - y
    return y


def greedy(x, max_pass=60):
    cur = x; hist = []; best_L = len(x); best_k = 0; stale = 0
    for _ in range(max_pass):
        bk = None
        for t, y in candidates(cur):
            key = (min(len(y), best_len(y)), len(y))
            if bk is None or key < bk[0]:
                bk = (key, t, y)
        (sc, ly), t, y = bk
        if sc >= len(cur) and ly >= len(cur):
            break
        hist.append(t); cur = y
        if len(cur) < best_L:
            best_L, best_k, stale = len(cur), len(hist), 0
        else:
            stale += 1
            if stale >= 3:
                break
    return best_L, hist[:best_k]


def exhaustive(x, depth=4):
    best = [len(x)] + [None] * depth
    def rec(cur, d):
        if d == depth - 1:
            L = best_len(cur)
            if best[depth] is None or L < best[depth]:
                best[depth] = L
            return
        for t, y in candidates(cur):
            if best[d + 1] is None or len(y) < best[d + 1]:
                best[d + 1] = len(y)
            rec(y, d + 1)
    rec(x, 0)
    return best


def per_block(x, B):
    raw = 0; tot = 0.0
    for i in range(0, len(x), B):
        blk = x[i:i + B]
        L, h = greedy(blk)
        raw += L
        side = 4 + ((len(h) * LOG12 + math.ceil(math.log2(2 * B + 2))) if h else 0)
        tot += L + side
    return raw, tot


def xz(b):
    return len(lzma.compress(b, preset=9 | lzma.PRESET_EXTREME))


def tobits(b):
    return np.unpackbits(np.frombuffer(b, dtype=np.uint8))


mode = sys.argv[1]
text = open('/usr/lib/python3.12/pydoc_data/topics.py', 'rb').read()
exe = open(sys.argv[2], 'rb').read() if len(sys.argv) > 2 else b''

if mode == 'whole':
    rng = np.random.default_rng(3)
    skew = (rng.random(2_000_000) >= 0.01).astype(np.uint8)
    datasets = [
        ('text 200KB', tobits(text[:200000])),
        ('executable 200KB', tobits(exe[500000:700000])),
        ('xz-compressed 200KB', tobits(lzma.compress(exe[:3_000_000])[:200000])),
        ('1% skew 2M bits', skew),
    ]
    for name, bits in datasets:
        t0 = time.time()
        raw = np.packbits(bits).tobytes()
        L, h = greedy(bits)
        ok = np.array_equal(inverse(forward(bits, h), h), bits)
        side = len(h) * LOG12 + 8
        ones = bits.mean() * 100
        print(f"{name}: ones {ones:.1f}% | raw {len(raw)} B | best menu result {math.ceil((L + side) / 8)} B "
              f"(change {((L + side) / len(bits) - 1) * 100:+.2f}%) passes {len(h)} | "
              f"xz {xz(raw)} B bz2 {len(bz2.compress(raw, 9))} B | lossless {ok} | {time.time() - t0:.0f}s", flush=True)
    p = np.mean(skew == 0)
    print("skew Shannon floor bytes:", round(len(skew) * (-(p * math.log2(p) + (1 - p) * math.log2(1 - p))) / 8))

elif mode == 'exhaustive':
    s = text[:50000]
    t0 = time.time()
    best = exhaustive(tobits(s), depth=4)
    print("exhaustive 50KB text, best bytes after 0..4 passes:", [math.ceil(b / 8) for b in best],
          "| xz", xz(s), "bz2", len(bz2.compress(s, 9)), f"| {time.time() - t0:.0f}s")

elif mode == 'blocks':
    bits = tobits(text[:200000])
    for B in (65536, 8192, 1024):
        t0 = time.time()
        raw, tot = per_block(bits, B)
        print(f"text per-block B={B}: oracle w/o selectors {math.ceil(raw / 8)} B, with selectors {math.ceil(tot / 8)} B "
              f"(orig 200000) | {time.time() - t0:.0f}s", flush=True)
    cb = tobits(lzma.compress(exe[:3_000_000])[:200000])
    raw, tot = per_block(cb, 1024)
    print(f"xz data per-block B=1024: oracle w/o selectors {math.ceil(raw / 8)} B, with selectors {math.ceil(tot / 8)} B (orig 200000)")

elif mode == 'threshold':
    rng = np.random.default_rng(5)
    for q in (0.30, 0.36, 0.38, 0.39, 0.42, 0.50):
        x = (rng.random(2_000_000) < q).astype(np.uint8)
        print(f"iid minority {q:.2f}: best single-pass change {(best_len(x) - len(x)) / len(x) * 100:+.2f}%")
