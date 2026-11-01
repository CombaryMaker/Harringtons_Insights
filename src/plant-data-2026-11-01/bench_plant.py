# bench_plant.py - industrial plant-data export test on REAL public data. Michael Hugh Harrington, 2026. MIT License.
# One archive, as a plant data export or backup would be: UCI SECOM (semiconductor fab sensors), UCI hydraulic test rig
# condition monitoring (pressure 100 Hz, flow, temperature, vibration, power), UCI appliances energy (building sensors),
# UCI steel industry energy, UCI air quality (includes an .xlsx, already compressed), Tennessee Eastman chemical plant
# benchmark (Braatz data), Numenta NAB machine and ambient temperature. 2 cores. Every decode compared byte for byte.
import subprocess, time, os
S = '/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
T = S + '/tools'; Z, H, HS = T + '/zstd', T + '/hcm', T + '/hs_zstd'
P = S + '/plant'; W = P + '/w'; os.makedirs(W, exist_ok=True)
def run(c):
    t = time.perf_counter(); subprocess.run(c, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); return time.perf_counter() - t
i = W + '/plant.tar'
if not os.path.exists(i):
    subprocess.run(f'cd {P}/data && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-01 -cf {i} .', shell=True, check=True)
n = os.path.getsize(i); ref = open(i, 'rb').read()
rows = [
 ('gzip -9',                       f'gzip -9 -c {i} > {W}/c', f'gzip -d -c {W}/c > {W}/o', W + '/c'),
 ('zstd -19',                      f'{Z} -q -f -19 {i} -o {W}/c', f'{Z} -q -f -d {W}/c -o {W}/o', W + '/c'),
 ('xz -9',                         f'xz -9 -c {i} > {W}/c', f'xz -d -c {W}/c > {W}/o', W + '/c'),
 ('zstd -19 -T2 (one stream)',     f'{Z} -q -f -19 -T2 {i} -o {W}/c', f'{Z} -q -f -d {W}/c -o {W}/o', W + '/c'),
]
for mb in (4, 16):
    for sets, lab in ((0, 'plain blocks'), (3, 'Harrington Sets')):
        rows.append((f'hs_zstd -19 {lab}, {mb} MB, 2 jobs', f'{HS} c {i} {W}/c --level 19 --jobs 2 --block-mb {mb} --sets {sets}', f'{HS} d {W}/c {W}/o --jobs 2', W + '/c'))
rows.append(('HCM v3.6.1 fast (one core)', f'{H} c {i} {W}/c', f'{H} d {W}/c {W}/o', W + '/c'))
rows.append(('HCM v3.6.1 Harrington Sets 16 MB (2 cores)', f'{H} s {i} {W}/c', f'{H} d {W}/c {W}/o', W + '/c'))
print(f'plant.tar {n:,} bytes ({len(os.listdir(P + "/data"))} datasets)')
print(f'{"method":44} {"bytes":>12} {"ratio":>7} {"comp s":>7} {"decode s":>8}  exact', flush=True)
for name, c, d, out in rows:
    for f in (W + '/c', W + '/o'):
        if os.path.exists(f): os.remove(f)
    tc = run(c); sz = os.path.getsize(out); td = run(d); ok = open(W + '/o', 'rb').read() == ref
    print(f'{name:44} {sz:>12,} {n/sz:>7.2f} {tc:>7.1f} {td:>8.2f}  {"yes" if ok else "NO"}', flush=True)
