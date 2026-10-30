#!/usr/bin/env python3
"""zip-hi: per-file search over WinZip-readable zip/zipx methods (Deflate, Deflate64, BZip2, LZMA, PPMd),
written by 7-Zip, smallest kept. Every candidate is extracted and checked byte-exact.
Records per candidate: size, compress CPU/peak memory, and extraction CPU/peak memory (7-Zip decoder,
single thread, stand-in until the WinZip check).
usage: zip_hi.py FILE OUTDIR [quick]   -> OUTDIR/FILE.json, keeps the best 3 archives."""
import sys, os, json, subprocess, itertools, filecmp, shutil
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi'))
from xz_hi import looks_binary, record_length
Z = os.path.expanduser('~/dl/7zip/CPP/7zip/Bundles/Alone2/b/g/7zz'); RUN = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'run1.py')
fn, od = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]); name = os.path.basename(fn)
quick = len(sys.argv) > 3
os.makedirs(od, exist_ok=True); data = open(fn, 'rb').read(); n = len(data)
def run(cmd, cwd=None):
    o = subprocess.run(['python3', RUN] + cmd, capture_output=True, text=True, cwd=cwd, check=True).stdout.split()
    return int(o[0]), float(o[1])
dbits = max(16, min(26, (n - 1).bit_length()))                       # dictionary sized to the file (64 KB .. 64 MB)
cands = [['-mm=Deflate', '-mx9', '-mpass=15', '-mfb=258'], ['-mm=Deflate64', '-mx9', '-mpass=15', '-mfb=257'],
         ['-mm=BZip2', '-mx9', '-mpass=10']]
lcpb = [(3, 0, 2), (0, 0, 0), (4, 0, 0), (1, 0, 0)]
if looks_binary(data):
    r = record_length(data[:4 << 20])
    for k in (1, 2):
        if r and r % (1 << k) == 0: lcpb.append((0, k, k))
    lcpb += [(0, 0, 0), (3, 0, 0)]
lcpb = list(dict.fromkeys(lcpb))
for (lc, lp, pb), fb in itertools.product(lcpb, (64, 273)):
    cands.append(['-mm=LZMA', '-mx9', '-md=%d' % dbits, '-mfb=%d' % fb, '-mlc=%d' % lc, '-mlp=%d' % lp, '-mpb=%d' % pb])
mems = [m for m in (16, 64, 256) if m == 16 or (m << 20) <= 12 * n or m == 64]
for o, m in itertools.product((4, 6, 8, 10, 12, 16), mems):
    cands.append(['-mm=PPMd', '-mo=%d' % o, '-mmem=%dm' % m])
if quick:   # screening on a sample is done by the caller; quick = drop the slowest PPMd memory size
    cands = [c for c in cands if '-mmem=256m' not in c]
res = []
work = os.path.join(od, name + '.tmp'); os.makedirs(work, exist_ok=True)
for i, c in enumerate(cands):
    arc = os.path.join(od, '%s.c%d.zip' % (name, i))
    if os.path.exists(arc): os.remove(arc)
    cmem, ccpu = run([Z, 'a', '-tzip', '-mmt1', '-bd', '-bso0', '-bsp0', arc, fn] + c, cwd=os.path.dirname(fn))
    shutil.rmtree(work); os.makedirs(work)
    dmem, dcpu = run([Z, 'x', '-mmt1', '-bd', '-bso0', '-bsp0', '-o' + work, arc])
    ok = filecmp.cmp(fn, os.path.join(work, name), shallow=False)
    res.append({'i': i, 'args': ' '.join(c), 'size': os.path.getsize(arc), 'ok': ok, 'c_cpu': ccpu, 'c_memKB': cmem, 'd_cpu': dcpu, 'd_memKB': dmem})
shutil.rmtree(work)
res.sort(key=lambda r: r['size'])
keep = {r['i'] for r in res[:3] if r['ok']}
for r in res:
    if r['i'] not in keep: os.remove(os.path.join(od, '%s.c%d.zip' % (name, r['i'])))
json.dump({'file': name, 'orig': n, 'cands': res}, open(os.path.join(od, name + '.json'), 'w'), indent=0)
b = res[0]; print(name, b['size'], b['args'], 'dec %.2fs %dMB' % (b['d_cpu'], b['d_memKB'] >> 10), flush=True)
