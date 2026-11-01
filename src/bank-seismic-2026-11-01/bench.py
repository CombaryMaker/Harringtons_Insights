# bench.py - seismic compression test on real public EarthScope data. Michael Hugh Harrington, 2026. MIT License.
# Every method is a reversible pipeline; every decode is compared byte for byte with the decoded samples.
import subprocess, time, os, sys, hashlib, glob
T = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/tools'
Q = os.path.dirname(os.path.abspath(__file__)); W = Q + '/w'; os.makedirs(W, exist_ok=True)
files = sorted(glob.glob(Q + '/i32/*.i32'))
only = sys.argv[1:]  # optional method names
def run(cmd):
    t = time.perf_counter(); subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); return time.perf_counter() - t
Z, P, H = T + '/zstd', T + '/pre', T + '/hcm'
# method: (name, compress steps, decompress steps); {i}=input {c}=compressed {o}=output {t}=temp
M = [
 ('zstd -19 on raw samples',            [f'{Z} -q -f -19 {{i}} -o {{c}}'],                                   [f'{Z} -q -f -d {{c}} -o {{o}}']),
 ('xz -9 on raw samples',               [f'xz -9 -k -c {{i}} > {{c}}'],                                       [f'xz -d -c {{c}} > {{o}}']),
 ('difference+shuffle, zstd -19',       [f'{P} ad {{i}} {{t}}', f'{Z} -q -f -19 {{t}} -o {{c}}'],            [f'{Z} -q -f -d {{c}} -o {{t}}', f'{P} uad {{t}} {{o}}']),
 ('difference+shuffle, xz -9',          [f'{P} ad {{i}} {{t}}', f'xz -9 -c {{t}} > {{c}}'],                  [f'xz -d -c {{c}} > {{t}}', f'{P} uad {{t}} {{o}}']),
 ('Harrington record delta, zstd -19',  [f'{P} rd {{i}} {{t}}', f'{Z} -q -f -19 {{t}} -o {{c}}'],            [f'{Z} -q -f -d {{c}} -o {{t}}', f'{P} urd {{t}} {{o}}']),
 ('Harrington record delta, xz -9',     [f'{P} rd {{i}} {{t}}', f'xz -9 -c {{t}} > {{c}}'],                  [f'xz -d -c {{c}} > {{t}}', f'{P} urd {{t}} {{o}}']),
 ('HCM v3.6.1 on raw samples',          [f'{H} c {{i}} {{c}}'],                                               [f'{H} d {{c}} {{o}}']),
 ('difference+shuffle, HCM v3.6.1',     [f'{P} ad {{i}} {{t}}', f'{H} c {{t}} {{c}}'],                       [f'{H} d {{c}} {{t}}', f'{P} uad {{t}} {{o}}']),
]
steim = sum(os.path.getsize(Q + '/raw/' + os.path.basename(f)[:-4] + '.mseed') for f in files)
raw = sum(os.path.getsize(f) for f in files)
st = 0
for f in files: st += run(f'{Q}/steim {Q}/raw/{os.path.basename(f)[:-4]}.mseed {W}/x.i32')
print(f'{len(files)} real station-day files (EarthScope, 2024-01-01, includes the M7.5 Noto earthquake), {raw:,} bytes as int32 samples')
print(f'{"method":38} {"bytes":>12} {"vs Steim2":>9} {"ratio":>6} {"comp s":>7} {"decode s":>8}  exact')
print(f'{"Steim2 in miniSEED (archive standard)":38} {steim:>12,} {1.0:>9.3f} {raw/steim:>6.2f} {"-":>7} {st:>8.2f}  yes (Xn checks)')
for name, cs, ds in M:
    if only and not any(o in name for o in only): continue
    tot = tc = td = 0; ok = True
    for f in files:
        sub = dict(i=f, c=W + '/c.bin', o=W + '/o.bin', t=W + '/t.bin')
        for s in cs: tc += run(s.format(**sub))
        tot += os.path.getsize(sub['c'])
        for s in ds: td += run(s.format(**sub))
        ok &= open(f, 'rb').read() == open(sub['o'], 'rb').read()
    print(f'{name:38} {tot:>12,} {steim/tot:>9.3f} {raw/tot:>6.2f} {tc:>7.1f} {td:>8.2f}  {"yes" if ok else "NO"}', flush=True)
