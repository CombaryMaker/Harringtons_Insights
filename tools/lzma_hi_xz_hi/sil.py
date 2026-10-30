#!/usr/bin/env python3
"""Silesia: LZMA -9 king vs lzma-hi (tuned 7-Zip LZMA) and xz-hi, with sample screening.
usage: sil.py FILE  -> writes results/FILE.json"""
import sys, os, json, lzma, itertools, subprocess
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi'))
from xz_hi import looks_binary, record_length
fn = sys.argv[1]; name = os.path.basename(fn)
data = open(fn, 'rb').read(); n = len(data)
os.makedirs('results', exist_ok=True); os.makedirs('silxz', exist_ok=True)
# sample: 4 chunks of 512 KB spread through the file
ck = 512 * 1024
samp = b''.join(data[int(n * f):int(n * f) + ck] for f in (0.1, 0.35, 0.6, 0.85))
sp = 'samp_%s.bin' % name
open(sp, 'wb').write(samp)

def lz(cfgs, path, reps):
    out = subprocess.run(['./lz', 'lzma', str(reps), path], input='\n'.join(cfgs) + '\n',
                         capture_output=True, text=True).stdout
    r = {}
    for l in out.splitlines():
        p = l.split()
        if p[0] == 'RES': r[p[1]] = (int(p[4]), float(p[5]), p[6])
    return r

# ---- lzma-hi: screen lc/lp/pb x fb on the sample, full-run top 3
grid = []
for lc, lp, pb in itertools.product(range(5), range(3), range(3)):
    if lc + lp <= 4:
        for fb in (273, 64):
            grid.append('lvl=9,lc=%d,lp=%d,pb=%d,fb=%d' % (lc, lp, pb, fb))
scr = lz(grid, sp, 1)
top = [c for c, _ in sorted(scr.items(), key=lambda kv: kv[1][0])[:3]]
full = lz(['lvl=9'] + top, fn, 3)

# ---- xz-hi with sample screening
def xzc(d, lc, lp, pb, pre=None, depth=512, nice=273):
    f = [] if pre is None else [pre]
    f.append({"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": max(4096, len(d)),
              "lc": lc, "lp": lp, "pb": pb, "nice_len": nice, "depth": depth, "mf": lzma.MF_BT4})
    return lzma.compress(d, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32, filters=f)
c3 = sorted(((len(xzc(samp, lc, lp, pb)), (lc, lp, pb)) for lc, lp, pb in itertools.product(range(5), range(3), range(3)) if lc + lp <= 4))[:2]
pres = [None]
if looks_binary(data):
    dists = {1, 2, 4, 8, 12, 13, 16, 26, 48, 216}
    r = record_length(data[:4 << 20])
    if 0 < r <= 256: dists.add(r)
    pres += [{"id": lzma.FILTER_DELTA, "dist": d} for d in sorted(dists)]
    pres += [{"id": i} for i in (lzma.FILTER_X86, lzma.FILTER_SPARC, lzma.FILTER_POWERPC, lzma.FILTER_ARM, lzma.FILTER_ARMTHUMB, lzma.FILTER_IA64)]
xs = []
for _, (lc, lp, pb) in c3:
    for pre in pres:
        try: xs.append((len(xzc(samp, lc, lp, pb, pre)), lc, lp, pb, pre))
        except lzma.LZMAError: pass
xs.sort(key=lambda t: t[0])
best = lzma.compress(data, format=lzma.FORMAT_XZ, preset=9 | lzma.PRESET_EXTREME)
open('silxz/%s.9e.xz' % name, 'wb').write(best)
xzbase = len(best)
for _, lc, lp, pb, pre in xs[:3]:
    b = xzc(data, lc, lp, pb, pre)
    if len(b) < len(best): best = b
assert lzma.decompress(best) == data
open('silxz/%s.hi.xz' % name, 'wb').write(best)
json.dump({'file': name, 'orig': n, 'lzma_full': full, 'lzma_top': top, 'xz9e': xzbase, 'xzhi': len(best)},
          open('results/%s.json' % name, 'w'))
os.remove(sp)
print(name, 'done')
