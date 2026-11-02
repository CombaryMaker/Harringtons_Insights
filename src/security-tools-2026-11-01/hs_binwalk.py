# hs_binwalk.py - split one large image into overlapping pieces, run binwalk on each in parallel, merge to the
# result plain binwalk gives. Michael Hugh Harrington, 2026. MIT License.
# Piece i is scanned over [start_i, end_i + overlap); only hits starting inside [start_i, end_i) are kept from it.
# A kept hit whose size reaches the edge of its scanned window was cut short by the split, so it is measured again
# alone on a longer slice (window x4 until it ends inside the slice). Each hit then claims [offset, offset+size), and
# hits that a later piece reports inside a claimed range are dropped, since plain binwalk does not scan inside a
# recognized item. Usage: hs_binwalk.py BINWALK IMAGE PIECES OVERLAP_MB OUT.json
import json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
bw, img, k, ov, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]) << 20, sys.argv[5]
retries = 0; n = os.path.getsize(img); step = -(-n // k); tmp = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(out)))
def scan(s, length, tag, only=None):
    f = f'{tmp}/{tag}.bin'; j = f'{tmp}/{tag}.json'
    with open(img, 'rb') as a, open(f, 'wb') as b: a.seek(s); b.write(a.read(length))
    c = [bw, '-q', '-t', '1', '-l', j, f] + (['-y', only] if only else [])
    global retries
    for t in range(6):   # binwalk 3.1.0 sometimes exits 0 without writing its -l log when several copies run at once
        subprocess.run(c, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(j): break
        retries += 1
    os.remove(f); return json.load(open(j))[0]['Analysis']['file_map']
def piece(i):
    s = i * step; e = min(n, s + step); w = min(n, e + ov)
    return [dict(h, offset=h['offset'] + s, _open=(h['offset'] + s + h['size'] >= w and w < n))
            for h in scan(s, w - s, f'p{i}') if h['offset'] + s < e]
def remeasure(h):
    L = 4 * (step + ov)
    while True:
        L2 = min(L, n - h['offset']); r = [x for x in scan(h['offset'], L2, f'r{h["offset"]}', h['name']) if x['offset'] == 0]
        if not r: return h
        if r[0]['size'] < L2 or h['offset'] + L2 >= n: return dict(r[0], offset=h['offset'])
        L *= 4
with ThreadPoolExecutor(k) as ex: parts = list(ex.map(piece, range(k)))
hits = []; claim = 0; nre = 0
for hs in parts:
    for h in sorted(hs, key=lambda h: h['offset']):
        if h['offset'] < claim: continue
        if h.pop('_open'): h = remeasure(h); nre += 1
        hits.append(h); claim = max(claim, h['offset'] + h['size'])
json.dump(hits, open(out, 'w')); print(f'{len(hits)} hits, {nre} re-measured, {retries} log retries')
