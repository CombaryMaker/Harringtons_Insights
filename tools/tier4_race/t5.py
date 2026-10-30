#!/usr/bin/env python3
"""HCM v3.3 with pre-filters: x86 E8/E9 transform and Combary activity map.
Per file and mode (fast/max): plain vs filtered; decode time = HCM decode + reverse filter. Verified exact.
Writes t5.json (resumable)."""
import os, sys, json, subprocess, time, filecmp
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi'))
from xz_hi import looks_binary, record_length
H = os.path.expanduser('~'); W = '/home/claude/t4/w5'; os.makedirs(W, exist_ok=True)
PRE = '/home/claude/t4/pre'; HF = '/home/claude/t4/w/hcm33'; HM = '/home/claude/t4/w/hcm33max'
sets = [(H + '/dl/cant/canterbury/', sorted(f for f in os.listdir(H + '/dl/cant/canterbury/') if f != 'SHA1SUM' and os.path.isfile(H + '/dl/cant/canterbury/' + f))),
        (H + '/sil/SilesiaCorpus/', sorted(f for f in os.listdir(H + '/sil/SilesiaCorpus/')
                                          if os.path.isfile(H + '/sil/SilesiaCorpus/' + f) and not f.endswith(('.zip', '.md'))))]
out = json.load(open('t5.json')) if os.path.exists('t5.json') else {}
def run(cmd):
    t0 = time.perf_counter(); r = subprocess.run(cmd, capture_output=True, text=True); t = time.perf_counter() - t0
    if r.returncode: raise RuntimeError(cmd, r.stderr)
    return t, r.stdout
for d, files in sets:
    corp = 'cant' if 'cant' in d else 'sil'
    for f in files:
        src = d + f; data = open(src, 'rb').read()
        variants = ['plain']
        if looks_binary(data): variants.append('e8e9')
        R = record_length(data[:4 << 20])
        if 0 < R <= 65536: variants.append('act')
        for v in variants:
            inp = src
            if v != 'plain':
                inp = W + '/pre.bin'
                args = [PRE, v, 'enc', src, inp] + ([str(R)] if v == 'act' else [])
                run(args)
            for mode, exe, flag in (('fast', HF, 'c'), ('max', HM, 'x')):
                key = '%s/%s|%s|%s' % (corp, f, v, mode)
                if key in out: continue
                cf = W + '/c.hcm'; df = W + '/d.bin'; rf = W + '/r.bin'
                run([exe, flag, inp, cf])
                td, _ = run([exe, 'd', cf, df])
                tp = 0.0
                if v != 'plain':
                    _, o = run([PRE, v, 'dec', df, rf]); tp = float(o.strip()) / 1000
                    final = rf
                else: final = df
                ok = filecmp.cmp(src, final, shallow=False)
                out[key] = {'orig': len(data), 'comp': os.path.getsize(cf), 'dec_s': td + tp, 'pre_s': tp, 'R': R, 'ok': ok}
                json.dump(out, open('t5.json', 'w'), indent=0)
                print(key, out[key], flush=True)
