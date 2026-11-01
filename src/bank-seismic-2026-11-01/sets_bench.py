# sets_bench.py - Harrington Sets (hs_zstd 1.3) vs plain blocks vs multi-threaded zstd, on the mixed bank backup and
# on all 8 seismic station-days joined into one file. Michael Hugh Harrington, 2026. MIT License. 2 cores, level 19.
import subprocess, time, os
S = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
T = S + '/tools'; Z, HS = T + '/zstd', T + '/hs_zstd'; W = S + '/bank/w'
def run(c):
    t = time.perf_counter(); subprocess.run(c, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); return time.perf_counter() - t
q = S + '/quake'
if not os.path.exists(W + '/seismic_all.i32'):
    subprocess.run(f'cat {q}/i32/*.i32 > {W}/seismic_all.i32', shell=True, check=True)
for f in ['mixed.tar', 'seismic_all.i32']:
    i = f'{W}/{f}'; n = os.path.getsize(i); ref = open(i, 'rb').read()
    print(f'\n=== {f}  {n:,} bytes, 2 jobs, level 19')
    print(f'{"method":40} {"bytes":>12} {"ratio":>7} {"comp s":>7} {"decode s":>8}  exact', flush=True)
    rows = [('zstd -19 -T2 (one stream)', f'{Z} -q -f -19 -T2 {i} -o {W}/s.zst', f'{Z} -q -f -d {W}/s.zst -o {W}/s.out', W + '/s.zst')]
    for mb in (4, 16):
        for sets, lab in ((0, 'plain blocks'), (3, 'Harrington Sets')):
            rows.append((f'hs_zstd {lab}, {mb} MB', f'{HS} c {i} {W}/s.hsz --level 19 --jobs 2 --block-mb {mb} --sets {sets}', f'{HS} d {W}/s.hsz {W}/s.out --jobs 2', W + '/s.hsz'))
    for name, c, d, out in rows:
        tc = run(c); sz = os.path.getsize(out); td = run(d); ok = open(W + '/s.out', 'rb').read() == ref
        print(f'{name:40} {sz:>12,} {n/sz:>7.2f} {tc:>7.1f} {td:>8.2f}  {"yes" if ok else "NO"}', flush=True)
