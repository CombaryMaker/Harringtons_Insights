# ours.py - Harrington Insights tools on the same silesia.tar that TurboBench measured. Michael Hugh Harrington, 2026.
# MIT License. Wall-clock times of whole command-line runs (TurboBench times in memory, so its speeds are higher by
# the file I/O and process start); 2-core cloud machine; every decode compared byte for byte.
import subprocess, time, os, shutil, sys
S = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
T = S + '/tools'; TB = S + '/tb'; W = TB + '/w'; os.makedirs(W, exist_ok=True)
src = TB + '/silesia.tar'; n = os.path.getsize(src); ref = open(src, 'rb').read()
def run(c):
    t = time.perf_counter(); subprocess.run(c, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); return time.perf_counter() - t
def clean():
    shutil.rmtree(W, ignore_errors=True); os.makedirs(W); shutil.copy(src, W + '/s.tar')
XZHI = S + '/fix/xzhi_lin'; ZPHI = S + '/zpaqhi/zpaq-hi'; H = T + '/hcm'; HS = T + '/hs_zstd'
rows = [
 ('xz-hi (stock xz decodes it)',              f'cd {W} && {XZHI} s.tar', f'cd {W} && mv s.tar s.orig && xz -d -k s.tar.xz', 's.tar.xz', 's.tar'),
 ('HCM v3.6.1 fast',                          f'{H} c {W}/s.tar {W}/s.hcm', f'{H} d {W}/s.hcm {W}/o.tar', 's.hcm', 'o.tar'),
 ('HCM v3.6.1 max',                           f'{H} x {W}/s.tar {W}/s.hcm', f'{H} d {W}/s.hcm {W}/o.tar', 's.hcm', 'o.tar'),
 ("HCM v3.6.1 Harrington's Sets 16 MB, 2 cores", f'{H} s {W}/s.tar {W}/s.hcm', f'{H} d {W}/s.hcm {W}/o.tar', 's.hcm', 'o.tar'),
 ("hs_zstd -19 Harrington's Sets 16 MB, 2 jobs", f'{HS} c {W}/s.tar {W}/s.hsz --level 19 --jobs 2 --block-mb 16 --sets 3', f'{HS} d {W}/s.hsz {W}/o.tar --jobs 2', 's.hsz', 'o.tar'),
 ('hs_zstd -19 plain blocks 16 MB, 2 jobs',   f'{HS} c {W}/s.tar {W}/s.hsz --level 19 --jobs 2 --block-mb 16 --sets 0', f'{HS} d {W}/s.hsz {W}/o.tar --jobs 2', 's.hsz', 'o.tar'),
 ('zpaq-hi -m5 (stock zpaq extracts it)',     f'cd {W} && {ZPHI} a s.zpaq s.tar -m5 -threads 1', f'cd {W} && mkdir x && cd x && {ZPHI} x ../s.zpaq -threads 1', 's.zpaq', 'x/s.tar'),
]
only = sys.argv[1:]
print(f'silesia.tar {n:,} bytes')
print(f'{"tool":46} {"bytes":>12} {"ratio%":>7} {"comp s":>8} {"decode s":>9}  exact', flush=True)
for name, c, d, out, back in rows:
    if only and not any(o in name for o in only): continue
    clean(); tc = run(c); sz = os.path.getsize(f'{W}/{out}'); td = run(d)
    ok = open(f'{W}/{back}', 'rb').read() == ref
    print(f'{name:46} {sz:>12,} {100*sz/n:>6.2f}% {tc:>8.1f} {td:>9.2f}  {"yes" if ok else "NO"}', flush=True)
