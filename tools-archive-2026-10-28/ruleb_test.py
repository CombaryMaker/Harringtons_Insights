import numpy as np, math, lzma, heapq, sys, time
SCR = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
exec(open(SCR + '/menu_search.py').read().split("mode = sys.argv[1]")[0])


def h0_bytes(data):
    f = np.bincount(np.frombuffer(data, np.uint8), minlength=256).astype(float)
    p = f[f > 0] / f.sum()
    return -(p * np.log2(p)).sum() * len(data) / 8


def b1_bits(data):
    # Rule B, max-skew version: most frequent characters get the 8-bit codes with the most 1s
    a = np.frombuffer(data, np.uint8)
    freq = np.bincount(a, minlength=256)
    order = np.argsort(-freq, kind='stable')
    pop_order = np.array(sorted(range(256), key=lambda v: (-bin(v).count('1'), v)), dtype=np.uint8)
    table = np.zeros(256, np.uint8); table[order] = pop_order
    distinct = int((freq > 0).sum())
    return np.unpackbits(table[a]), distinct * 8 + 8


def b2_bits(data):
    # Rule B, Huffman version, branches labeled so the heavier side is 1 (pushes balance toward 1s)
    a = np.frombuffer(data, np.uint8)
    freq = np.bincount(a, minlength=256)
    heap = [(int(c), i, ('L', i)) for i, c in enumerate(freq) if c > 0]
    heapq.heapify(heap); k = 256
    while len(heap) > 1:
        c1, _, n1 = heapq.heappop(heap); c2, _, n2 = heapq.heappop(heap)
        k += 1; heapq.heappush(heap, (c1 + c2, k, ('N', n1, n2)))
    codes = {}; stack = [(heap[0][2], '')]
    while stack:
        n, pre = stack.pop()
        if n[0] == 'L':
            codes[n[1]] = pre or '0'
        else:
            stack.append((n[1], pre + '0')); stack.append((n[2], pre + '1'))
    s = ''.join(codes[b] for b in a.tolist())
    return (np.frombuffer(s.encode(), np.uint8) - 48).astype(np.uint8), len(codes) * 10 + 16


def menu(bits, table_bits=0):
    L, h = greedy(bits)
    ok = np.array_equal(inverse(forward(bits, h), h), bits)
    return math.ceil((L + len(h) * LOG12 + 8 + table_bits) / 8), len(h), ok


text = open('/usr/lib/python3.12/pydoc_data/topics.py', 'rb').read()[:200000]
exe = open('/usr/lib/x86_64-linux-gnu/libpython3.12.so.1.0', 'rb').read()[500000:700000]

for name, data in (('text', text), ('executable', exe)):
    n = len(data)
    pct = lambda b: f"{(b / n - 1) * 100:+.1f}%"
    raw = tobits(data)
    m0 = menu(raw)
    b1, t1 = b1_bits(data)
    m1 = menu(b1, t1)
    b2, t2 = b2_bits(data)
    b2size = math.ceil((len(b2) + t2) / 8)
    m2 = menu(b2, t2)
    print(f"== {name} ({n} B) | per-character floor (order-0 entropy) {pct(h0_bytes(data))} | xz {pct(xz(data))}")
    print(f"  menu alone:            ones {raw.mean()*100:.1f}%  -> {pct(m0[0])} ({m0[1]} passes, lossless {m0[2]})")
    print(f"  B max-skew relabel:    ones {b1.mean()*100:.1f}%  alone {pct(n + t1/8)}  + menu {pct(m1[0])} ({m1[1]} passes, lossless {m1[2]})")
    print(f"  B Huffman (heavy=1):   ones {b2.mean()*100:.1f}%  alone {pct(b2size)}  + menu {pct(m2[0])} ({m2[1]} passes, lossless {m2[2]})", flush=True)
