import subprocess, time, os, sys, shutil, filecmp
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
ST=S+'/zpaqbin'; HI=S+'/zpaqhi/zpaq-hi'
def run(cmd, cwd, env=None):
    t=time.perf_counter(); r=subprocess.run(cmd, cwd=cwd, capture_output=True, env=env); return time.perf_counter()-t, r
def one(path, W):
    f=os.path.basename(path); d=os.path.join(W, f+'_d'); shutil.rmtree(d, ignore_errors=True); os.makedirs(d); shutil.copy(path, d)
    out={}
    for tag, exe in (('stock', ST), ('hi', HI)):
        a=os.path.join(d, tag+'.zpaq')
        env=dict(os.environ, ZPAQHI_VERBOSE='1')
        ct, r = run([exe, 'a', a, f, '-m5'], d, env)
        x=os.path.join(d, 'x_'+tag); os.makedirs(x)
        dt, _ = run([ST, 'x', a, '-to', x], d)       # decode always with STOCK zpaq
        ok=filecmp.cmp(path, os.path.join(x, f), shallow=False)
        chose=[l for l in r.stderr.decode().splitlines() if 'chose' in l]
        out[tag]=(os.path.getsize(a), ct, dt, ok, chose[0] if chose else '')
    return out
if __name__=='__main__':
    W=sys.argv[1]; os.makedirs(W, exist_ok=True)
    for p in sys.argv[2:]:
        o=one(p, W); s, h = o['stock'], o['hi']
        print(f"{os.path.basename(p):14s} stock {s[0]:>9,} ct {s[1]:6.1f} dt {s[2]:6.1f} | hi {h[0]:>9,} ct {h[1]:6.1f} dt {h[2]:6.1f} ok={h[3]} {h[4]}", flush=True)
