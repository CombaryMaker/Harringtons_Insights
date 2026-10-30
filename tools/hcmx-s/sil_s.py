#!/usr/bin/env python3
"""hcmx-s speed check on Silesia (fast mode). Per file, sequential, CPU-time decode, verified exact.
old = hcmx (Oct 27 rebuild), new = hcmx-s (same output, fixed-size loops), lite = gated pointer, x86 = call filter.
Decodes of old and new alternate on the same compressed file. Writes sil_s.json."""
import os, sys, json, subprocess, resource, filecmp
sys.path.insert(0, os.path.expanduser('~/Harringtons_Insights/xz-hi')); from xz_hi import looks_binary
OLD = '/root/hcmx/hcmx'; NEW = '/root/hcmx/hcmx-s'; S = os.path.expanduser('~/sil/SilesiaCorpus/'); W = '/home/claude/hcmx_t/ws'
os.makedirs(W, exist_ok=True)
files = sorted(f for f in os.listdir(S) if os.path.isfile(S + f) and not f.endswith(('.zip', '.md')))
out = json.load(open('sil_s.json')) if os.path.exists('sil_s.json') else {}
def enc(exe, mode, src, dst): subprocess.run([exe, 'c', src, dst], env=dict(os.environ, HCMX_ONLY=str(mode)), check=True, capture_output=True)
def dec(exe, src, dst):
    if os.path.exists(dst): os.remove(dst)
    r0 = resource.getrusage(resource.RUSAGE_CHILDREN); subprocess.run([exe, 'd', src, dst], check=True, capture_output=True)
    r1 = resource.getrusage(resource.RUSAGE_CHILDREN); return r1.ru_utime - r0.ru_utime + r1.ru_stime - r0.ru_stime
for f in files:
    src = S + f; data = open(src, 'rb').read(); b = 16 if looks_binary(data) else 0
    x86 = b and data.count(b'\xe8') * 200 > len(data)
    tests = [('v3.3', 32 | b, [NEW]), ('trit2', 96 | b, [OLD, NEW, OLD, NEW]), ('lite', 104 | b, [NEW])]
    if b: tests += [('bin1', 240, [OLD, NEW, OLD, NEW]), ('bin1-lite', 248, [NEW])]
    if x86: tests += [('trit2-x86', 114, [NEW]), ('bin1-x86', 242, [NEW])]
    for name, mode, exes in tests:
        key = f + '|' + name
        if key in out: continue
        cf, df = W + '/c', W + '/d'
        enc(NEW, mode, src, cf)
        t = {'old': [], 'new': []}
        for e in exes: t['old' if e == OLD else 'new'].append(dec(e, cf, df))
        ok = filecmp.cmp(src, df, shallow=False)
        out[key] = {'orig': len(data), 'comp': os.path.getsize(cf), 'old': min(t['old']) if t['old'] else None, 'new': min(t['new']), 'ok': ok}
        json.dump(out, open('sil_s.json', 'w'), indent=0); print(key, out[key], flush=True)
