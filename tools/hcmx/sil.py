#!/usr/bin/env python3
"""hcm-trit v2 / HCM-Bin v1 rebuild test on Canterbury: per file, per variant, fast and max.
Variants (mode bits): v3.3=32(+16 bin), trit2=96(+16), bin1=240 (pointer+record ctx), bin1-noptr=244.
Verified byte-exact; decode timed. Writes sil.json."""
import os, sys, json, subprocess, time, filecmp
X = '/root/hcmx/hcmx'; D = os.path.expanduser('~/sil/SilesiaCorpus/'); W = '/home/claude/hcmx_t/w'; os.makedirs(W, exist_ok=True)
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi')); from xz_hi import looks_binary
files = sorted(f for f in os.listdir(D) if os.path.isfile(D + f) and not f.endswith(('.zip', '.md')))
out = {}
for mode, mbit in (('fast', 0),):
    for f in files:
        data = open(D + f, 'rb').read(); b = 16 if looks_binary(data) else 0
        vs = {'v3.3': 32 | b, 'trit2': 96 | b}
        if b: vs.update({'bin1': 240})
        for v, m in vs.items():
            env = dict(os.environ, HCMX_ONLY=str(m | mbit))
            cf, df = W + '/c', W + '/d'
            t0 = time.perf_counter(); subprocess.run([X, 'c', D + f, cf], env=env, check=True, capture_output=True); ct = time.perf_counter() - t0
            t0 = time.perf_counter(); subprocess.run([X, 'd', cf, df], check=True, capture_output=True); dt = time.perf_counter() - t0
            ok = filecmp.cmp(D + f, df, shallow=False)
            out['%s|%s|%s' % (mode, f, v)] = {'orig': len(data), 'comp': os.path.getsize(cf), 'ct': ct, 'dt': dt, 'ok': ok}
            print(mode, f, v, os.path.getsize(cf), ok, '%.2f' % dt, flush=True)
            os.remove(cf); os.remove(df)
json.dump(out, open('/home/claude/hcmx_t/sil.json', 'w'), indent=0)
