"""Synthetic mixed record stream (JSON Lines): three sources in long sections (0.5-4 MB), 1% planted anomalies.
Usage: make_data.py OUT MB SEED   (labels written to OUT.labels, one 0/1 per record)"""
import json, random, sys
out, mb, seed = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]); R = random.Random(seed)
words = 'user login ok logout cache miss hit write read sync retry timeout queue job done start stop'.split()
def rec(src, n, bad):
    if src == 'env':
        r = {'src': 'env', 'id': n, 'f1': round(R.gauss(23, 2), 2), 'f2': round(R.gauss(45, 5), 1), 'f3': round(R.gauss(1013, 4), 1)}
        if bad: r['f1'] = round(R.gauss(40, 2), 2)
    elif src == 'net':
        r = {'src': 'net', 'id': n, 'f1': int(R.gauss(180000, 30000)), 'f2': int(R.gauss(1200, 200)), 'f3': round(R.gauss(12, 3), 1), 'host': f'srv-{R.randint(1, 40)}'}
        if bad: r['f3'] = round(R.gauss(40, 4), 1)
    else:
        r = {'src': 'app', 'id': n, 'f1': int(R.gauss(120, 25)), 'f2': R.choice([200, 200, 200, 304]), 'f3': R.randint(20, 60),
             'msg': ' '.join(R.choice(words) for _ in range(R.randint(4, 10)))}
        if bad: r['f1'] = int(R.gauss(400, 30))
    return json.dumps(r, separators=(',', ':')) + '\n'
size = 0; n = 0; target = mb * 2**20
with open(out, 'w') as f, open(out + '.labels', 'w') as g:
    while size < target:
        src = R.choice(['env', 'net', 'app']); sec = R.uniform(0.5, 4) * 2**20; s0 = size
        while size - s0 < sec and size < target:
            bad = R.random() < 0.01; line = rec(src, n, bad); f.write(line); g.write('1\n' if bad else '0\n'); size += len(line); n += 1
print(n, 'records', size, 'bytes')
