# bench_bank.py - compression test on SYNTHETIC bank data in public formats (gen_bank.py). Michael Hugh Harrington,
# 2026. MIT License. Three archives, as a bank would keep them: all ACH files, all ATM journals, and one mixed daily
# backup (each day's ACH files and journals together). Every decode is compared byte for byte.
import subprocess, time, os, glob, sys
T = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/tools'
B = os.path.dirname(os.path.abspath(__file__)); W = B + '/w'; os.makedirs(W, exist_ok=True)
Z, P, H, HS = T + '/zstd', T + '/pre', T + '/hcm', T + '/hs_zstd'
def run(cmd):
    t = time.perf_counter(); subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); return time.perf_counter() - t
# archives (tar with fixed metadata so they are reproducible)
TAR = 'tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-01 -cf'
if not os.path.exists(W + '/ach.tar'):
    subprocess.run(f'cd {B} && {TAR} {W}/ach.tar ach && {TAR} {W}/ej.tar ej', shell=True, check=True)
    days = sorted({os.path.basename(f)[:8] for f in glob.glob(B + '/ach/*.ach')})
    lst = []
    for d in days: lst += sorted(glob.glob(f'{B}/ach/{d}_*')) + sorted(glob.glob(f'{B}/ej/{d}_*'))
    open(W + '/mixed.list', 'w').write('\n'.join(os.path.relpath(f, B) for f in lst) + '\n')
    subprocess.run(f'cd {B} && tar --owner=0 --group=0 --numeric-owner --mtime=2026-10-01 -cf {W}/mixed.tar -T {W}/mixed.list', shell=True, check=True)
M = [
 ('gzip -9',                            [f'gzip -9 -c {{i}} > {{c}}'],                                       [f'gzip -d -c {{c}} > {{o}}']),
 ('zstd -19',                           [f'{Z} -q -f -19 {{i}} -o {{c}}'],                                   [f'{Z} -q -f -d {{c}} -o {{o}}']),
 ('xz -9',                              [f'xz -9 -c {{i}} > {{c}}'],                                         [f'xz -d -c {{c}} > {{o}}']),
 ('Harrington record delta + zstd -19', [f'{P} rd {{i}} {{t}}', f'{Z} -q -f -19 {{t}} -o {{c}}'],            [f'{Z} -q -f -d {{c}} -o {{t}}', f'{P} urd {{t}} {{o}}']),
 ('Harrington record delta + xz -9',    [f'{P} rd {{i}} {{t}}', f'xz -9 -c {{t}} > {{c}}'],                  [f'xz -d -c {{c}} > {{t}}', f'{P} urd {{t}} {{o}}']),
 ('HCM v3.6.1 fast',                    [f'{H} c {{i}} {{c}}'],                                               [f'{H} d {{c}} {{o}}']),
]
only = sys.argv[1:]
for arch in ['ach', 'ej', 'mixed']:
    i = f'{W}/{arch}.tar'; n = os.path.getsize(i)
    print(f'\n=== {arch}.tar  {n:,} bytes')
    print(f'{"method":38} {"bytes":>12} {"ratio":>7} {"comp s":>7} {"decode s":>8}  exact', flush=True)
    for name, cs, ds in M:
        if only and not any(o in name for o in only): continue
        sub = dict(i=i, c=W + '/c.bin', o=W + '/o.bin', t=W + '/t.bin')
        tc = sum(run(s.format(**sub)) for s in cs); sz = os.path.getsize(sub['c'])
        td = sum(run(s.format(**sub)) for s in ds); ok = open(i, 'rb').read() == open(sub['o'], 'rb').read()
        print(f'{name:38} {sz:>12,} {n/sz:>7.2f} {tc:>7.1f} {td:>8.2f}  {"yes" if ok else "NO"}', flush=True)
