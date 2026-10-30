#!/usr/bin/env python3
"""7z -mx9 king vs 7z-hi (per-file LZMA2 + filter settings, stock 7-Zip reads it).
usage: z7.py FILE OUTDIR [lzma_top_json]   -> OUTDIR/FILE.json with sizes of every candidate (archives kept)"""
import sys, os, json, subprocess, itertools
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi'))
from xz_hi import looks_binary, record_length
Z = os.path.expanduser('~/dl/7zip/CPP/7zip/Bundles/Alone2/b/g/7zz')
fn, od = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]); name = os.path.basename(fn)
os.makedirs(od, exist_ok=True)
data = open(fn, 'rb').read()
lcpb = [(3, 0, 2)]
if len(sys.argv) > 3 and os.path.exists(sys.argv[3]):
    for c in json.load(open(sys.argv[3]))['lzma_top']:
        kv = dict(x.split('=') for x in c.split(','))
        t = (int(kv['lc']), int(kv['lp']), int(kv['pb']))
        if t not in lcpb: lcpb.append(t)
else:
    lcpb += [(3, 0, 0), (0, 0, 0), (1, 0, 0)]
filters = ['on']                       # 7-Zip's own automatic choice (what -mx9 does)
if looks_binary(data):
    filters += ['off', 'BCJ', 'BCJ2', 'ARM64']
    r = record_length(data[:4 << 20])
    for d in sorted({2, 4} | ({r} if 0 < r <= 256 else set())):
        filters.append('Delta:%d' % d)
else:
    filters += ['off']
def run(tag, args):
    arc = os.path.join(od, '%s.%s.7z' % (name, tag))
    if os.path.exists(arc): os.remove(arc)
    subprocess.run([Z, 'a', '-t7z', '-mmt1', '-bd', '-bso0', '-bsp0', arc, fn] + args, check=True,
                   cwd=os.path.dirname(fn))
    return os.path.getsize(arc)
res = {'base': run('base', ['-mx9'])}
i = 0
for (lc, lp, pb), fb, f in itertools.product(lcpb, (64, 273), filters):
    m = ['-mx9', '-m0=LZMA2:d=64m:fb=%d:lc=%d:lp=%d:pb=%d' % (fb, lc, lp, pb)]
    if f != 'on': m.append('-mf=%s' % f)
    tag = 'c%d' % i; i += 1
    try: res[tag] = (run(tag, m), ' '.join(m[1:]))
    except subprocess.CalledProcessError: pass
# keep only base + 3 smallest archives on disk
cands = sorted((v[0], k) for k, v in res.items() if k != 'base')
for _, k in cands[3:]:
    os.remove(os.path.join(od, '%s.%s.7z' % (name, k)))
json.dump({'file': name, 'orig': len(data), 'base': res['base'], 'cands': {k: res[k] for _, k in cands[:3]}},
          open(os.path.join(od, name + '.json'), 'w'))
print(name, 'done', flush=True)
