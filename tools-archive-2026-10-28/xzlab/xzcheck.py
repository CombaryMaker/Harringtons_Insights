import lzma, json, os, subprocess, re
from xzsearch import comp, C
res = json.load(open('xzsearch.json'))
def fixpre(p):
    return None if p is None else {k: v for k, v in p.items()}
def dec_time(path, n, reps):
    o = subprocess.run(['./xzdec_bench', path, str(n), str(reps)], capture_output=True, text=True).stdout
    m = re.search(r'best ([\d.]+) us  median ([\d.]+) us', o); return float(m.group(2)), float(m.group(1))
tot_b = tot_n = 0; tb = tn = 0
for fn, bsize, out in res:
    data = open(os.path.join(C, fn), 'rb').read()
    base = lzma.compress(data, format=lzma.FORMAT_XZ, preset=9 | lzma.PRESET_EXTREME)
    open(f'{fn}.base.xz', 'wb').write(base)
    reps = 400 if len(data) < 200000 else 100
    bmed, _ = dec_time(f'{fn}.base.xz', len(data), reps)
    chosen = None
    for n, c in out + [[len(base), None]]:
        if c is None: chosen = (len(base), 'xz -9e (no better setting passed)', bmed); break
        blob = comp(data, c['lc'], c['lp'], c['pb'], fixpre(c['pre']), c['depth'], c['nice'])
        open(f'{fn}.new.xz', 'wb').write(blob)
        # stock xz must decode it exactly
        ok = subprocess.run(f'xz -dc {fn}.new.xz | cmp -s - {C}/{fn}', shell=True).returncode == 0
        nmed, _ = dec_time(f'{fn}.new.xz', len(data), reps)
        if ok and nmed <= bmed * 1.01 and len(blob) <= len(base):
            chosen = (len(blob), c, nmed); break
    tot_b += len(base); tot_n += chosen[0]; tb += bmed; tn += chosen[2]
    print(f"{fn:13s} xz -9e {len(base):6d} ({bmed:8.1f} us)  ->  {chosen[0]:6d} ({chosen[2]:8.1f} us)  {chosen[1]}")
tin = sum(os.path.getsize(os.path.join(C, r[0])) for r in res)
print(f"ALL 11: 1,000,000 bits down to {tot_b*1e6/tin:.0f} -> {tot_n*1e6/tin:.0f}; decode {tb/1000:.1f} ms -> {tn/1000:.1f} ms")
