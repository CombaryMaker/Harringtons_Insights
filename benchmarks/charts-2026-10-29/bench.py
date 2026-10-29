# Harrington Insights benchmark harness (29 Oct 2026).
# Every compressor runs once per file on this machine: compressed size, compression wall time,
# decompression wall time (both including program start-up) and peak memory (os.wait4 maxrss).
# Every decompressed file is compared byte for byte with the original.
import os, sys, json, time, glob, shutil, subprocess, tempfile
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
OUT=S+'/bench/results.json'
ZSTD=S+'/z3/zstd/programs/zstd'; LZ4=S+'/bl/lz4/programs/lz4'; ZPAQ=S+'/zpaqbin'
LP=S+'/lpaq/lpaq1'; LPH=S+'/lpaq/lpaq1h'; HCM=S+'/fix/hcm34b'; HCMX=S+'/fix/hcmx34b'
XZHI=S+'/fix/xzhi_lin'; BZHI=S+'/winbuild/bzip2-hi-linux'
def run(cmd, cwd=None, env=None):
    t=time.perf_counter(); p=subprocess.Popen(cmd, shell=True, cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _,status,ru=os.wait4(p.pid,0); el=time.perf_counter()-t
    if status!=0: raise RuntimeError(f'failed: {cmd}')
    return el, ru.ru_maxrss/1024.0
# kind: Standard / Modified (stock decoder reads it) / Mutant (own decoder) / Original
TOOLS={
 'lz4 -1':      ('Standard', lambda i,o,w: run(f'{LZ4} -1 -q -f {i} {o}'), lambda o,d,w: run(f'{LZ4} -d -q -f {o} {d}')),
 'gzip -9':     ('Standard', lambda i,o,w: run(f'gzip -9 -c {i} > {o}'), lambda o,d,w: run(f'gzip -dc {o} > {d}')),
 'bzip2 -9':    ('Standard', lambda i,o,w: run(f'bzip2 -9 -c {i} > {o}'), lambda o,d,w: run(f'bzip2 -dc {o} > {d}')),
 'zstd -3':     ('Standard', lambda i,o,w: run(f'{ZSTD} -3 -q -f {i} -o {o}'), lambda o,d,w: run(f'{ZSTD} -d -q -f {o} -o {d}')),
 'zstd -19':    ('Standard', lambda i,o,w: run(f'{ZSTD} -19 -q -f {i} -o {o}'), lambda o,d,w: run(f'{ZSTD} -d -q -f {o} -o {d}')),
 'xz -9e':      ('Standard', lambda i,o,w: run(f'xz -9e -c {i} > {o}'), lambda o,d,w: run(f'xz -dc {o} > {d}')),
 'lpaq1 -6':    ('Standard', lambda i,o,w: run(f'{LP} 6 {i} {o}'), lambda o,d,w: run(f'{LP} d {o} {d}')),
 'ZPAQ -m5':    ('Standard', lambda i,o,w: run(f'rm -f {o} && cp {i} {w}/f && cd {w} && {ZPAQ} a {o} f -m5 && rm f'), lambda o,d,w: run(f'cd {w} && rm -f f && {ZPAQ} x {o} -force && mv f {d}')),
 'bzip2-hi':    ('Modified', lambda i,o,w: run(f'cp {i} {w}/f && {BZHI} {w}/f && mv {w}/f.bz2 {o} && rm -f {w}/f'), lambda o,d,w: run(f'bzip2 -dc {o} > {d}')),
 'xz-hi':       ('Modified', lambda i,o,w: run(f'cp {i} {w}/f && {XZHI} {w}/f && mv {w}/f.xz {o} && rm -f {w}/f'), lambda o,d,w: run(f'xz -dc {o} > {d}')),
 'HCM v3.4 fast':('Original', lambda i,o,w: run(f'{HCM} c {i} {o}'), lambda o,d,w: run(f'{HCM} d {o} {d}')),
 'HCM v3.4 max': ('Original', lambda i,o,w: run(f'{HCM} x {i} {o}'), lambda o,d,w: run(f'{HCM} d {o} {d}')),
 'hcmx fast':   ('Original', lambda i,o,w: run(f'{HCMX} c {i} {o}'), lambda o,d,w: run(f'{HCMX} d {o} {d}')),
 'lpaq1-h':     ('Mutant', None, lambda o,d,w: run(f'{LPH} d {o} {d}')),
}
def lpaq1h_comp(i,o,w):
    best=None; tt=0; mm=0
    for fl in (0,4,9,13,11,15):
        el,m=run(f'{LPH} 6 {i} {w}/t.c', env=dict(os.environ,LPH_FLAGS=str(fl))); tt+=el; mm=max(mm,m)
        s=os.path.getsize(f'{w}/t.c')
        if best is None or s<best: best=s; shutil.move(f'{w}/t.c',o)
    return tt,mm
C=os.path.dirname(glob.glob('/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cant*/**/alice29.txt',recursive=True)[0]) if glob.glob('/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cant*/**/alice29.txt',recursive=True) else sys.argv[1]
SETS={'Canterbury':[f for f in sorted(glob.glob(C+'/*')) if 'SHA1' not in f and 'README' not in f],
      'Silesia 1 MB sample':sorted(glob.glob(S+'/lpaq/s1m/*')),
      'Synthetic (Goodwin)':sorted(glob.glob(S+'/fix/syn/d/*'))}
res=json.load(open(OUT)) if os.path.exists(OUT) else {}
w=tempfile.mkdtemp(dir=S+'/bench')
only=os.environ.get('ONLY')
for sname,files in SETS.items():
    for tname,(kind,comp,dec) in TOOLS.items():
        if only and tname not in only.split(','): continue
        key=f'{sname}|{tname}'
        if key in res: continue
        per={}
        for f in files:
            o=f'{w}/c.bin'; d=f'{w}/d.bin'
            ct,cm=(lpaq1h_comp if tname=='lpaq1-h' else comp)(f,o,w)
            dt,dm=dec(o,d,w)
            if open(d,'rb').read()!=open(f,'rb').read(): raise RuntimeError(f'ROUNDTRIP FAIL {tname} {f}')
            size=os.path.getsize(o)
            if tname=='ZPAQ -m5': size-=1015   # archive overhead removed, as in Goodwin's review (lower bound)
            per[os.path.basename(f)]=dict(orig=os.path.getsize(f),comp=size,ct=round(ct,4),dt=round(dt,4),cram=round(cm,1),dram=round(dm,1))
        res[key]=dict(set=sname,tool=tname,kind=kind,files=per)
        tot={k:sum(v[k] for v in per.values()) for k in ('orig','comp','ct','dt')}
        print(f'{sname:22s} {tname:14s} {tot["comp"]:>10,} ct {tot["ct"]:7.1f}s dt {tot["dt"]:7.1f}s',flush=True)
        json.dump(res,open(OUT,'w'),indent=0)
shutil.rmtree(w,ignore_errors=True)
