#!/usr/bin/env python3
"""Tier-4 race on Silesia: HCM v3.3 (fast/max) vs ZPAQ, bzip3, BSC, with lz4 as speed reference.
Sequential, one thread each, decode to a temp file, verified byte-exact. Writes t4.json."""
import os, subprocess, time, json, filecmp, shutil, sys
H = os.path.expanduser('~'); S = H + '/enw/'
W = '/home/claude/t4/w'; os.makedirs(W, exist_ok=True)
shutil.copy(H + '/hcm33', W + '/hcm33'); shutil.copy(H + '/hcm33', W + '/hcm33max')
B = {
 'lz4':      (lambda i, o: [H+'/sil/lz4/lz4', '-q', '-f', i, o],            lambda i, o: [H+'/sil/lz4/lz4', '-d', '-q', '-f', i, o]),
 'hcm-fast': (lambda i, o: [W+'/hcm33', 'c', i, o],                          lambda i, o: [W+'/hcm33', 'd', i, o]),
 'hcm-max':  (lambda i, o: [W+'/hcm33max', 'x', i, o],                       lambda i, o: [W+'/hcm33max', 'd', i, o]),
 'bzip3':    (lambda i, o: [H+'/dl/bzip3/b/bzip3', '-e', '-f', '-j', '1', i, o], lambda i, o: [H+'/dl/bzip3/b/bzip3', '-d', '-f', '-j', '1', i, o]),
 'bsc':      (lambda i, o: [H+'/dl/libbsc/b/bsc', 'e', i, o],                lambda i, o: [H+'/dl/libbsc/b/bsc', 'd', i, o]),
 'bsc-max':  (lambda i, o: [H+'/dl/libbsc/b/bsc', 'e', i, o, '-b64e2'],      lambda i, o: [H+'/dl/libbsc/b/bsc', 'd', i, o]),
 'zpaq-m4':  None, 'zpaq-m5': None,
}
Z = H + '/dl/zpaq/zpaq'
files = sys.argv[1:] or sorted(f for f in os.listdir(S) if not f.endswith(('.zip', '.md')) and os.path.isfile(S + f))
out = json.load(open('t4e.json')) if os.path.exists('t4e.json') else {}
for f in files:
    src = S + f; n = os.path.getsize(src)
    for name, cmd in B.items():
        key = f + '|' + name
        if key in out: continue
        cf = W + '/c.bin'; df = W + '/d.bin'
        for p in (cf, df):
            if os.path.exists(p): os.remove(p)
        if name.startswith('zpaq'):
            m = name[-1]
            shutil.copy(src, W + '/' + f)
            t0 = time.perf_counter()
            subprocess.run([Z, 'a', cf, f, '-m' + m, '-t1'], cwd=W, check=True, capture_output=True)
            ct = time.perf_counter() - t0
            dd = W + '/dz'; shutil.rmtree(dd, ignore_errors=True)
            t0 = time.perf_counter()
            subprocess.run([Z, 'x', cf, '-to', dd, '-t1', '-force'], cwd=W, check=True, capture_output=True)
            dt = time.perf_counter() - t0
            ok = filecmp.cmp(src, dd + '/' + f, shallow=False)
            os.remove(W + '/' + f); shutil.rmtree(dd, ignore_errors=True)
        else:
            c, d = cmd
            t0 = time.perf_counter(); subprocess.run(c(src, cf), check=True, capture_output=True); ct = time.perf_counter() - t0
            reps = 5 if name == 'lz4' else 1
            ts = []
            for _ in range(reps):
                if os.path.exists(df): os.remove(df)
                t0 = time.perf_counter(); subprocess.run(d(cf, df), check=True, capture_output=True); ts.append(time.perf_counter() - t0)
            dt = sorted(ts)[len(ts) // 2]
            ok = filecmp.cmp(src, df, shallow=False)
        out[key] = {'orig': n, 'comp': os.path.getsize(cf), 'ctime': ct, 'dtime': dt, 'ok': ok}
        json.dump(out, open('t4e.json', 'w'), indent=0)
        print(key, out[key], flush=True)
