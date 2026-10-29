import subprocess, time, os, filecmp, sys, shutil
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; W=S+'/parbench'
LP=S+'/lpaq/lpaq1h'; PAR='/home/claude/harringtons_insights/src/lpaq1-h/lpaq1h_par.py'; BEST='/home/claude/harringtons_insights/src/lpaq1-h/lpaq1h_best.py'
ZH=S+'/zpaqhi/zpaq-hi'; ZS=S+'/zpaqbin'
def t(cmd, cwd=None):
    a=time.perf_counter(); r=subprocess.run(cmd, capture_output=True, cwd=cwd); 
    if r.returncode: print('FAIL', cmd, r.stderr.decode()[:300], flush=True)
    return time.perf_counter()-a
def row(f, n, tag, z, ct, dt, ok): print(f'{f:9s} {tag:30s} {z:>10,} B  1M bits->{z/n*1e6:>8,.0f}  ratio {n/z:6.3f}  compress {ct:6.1f} s  decode {dt:6.1f} s  exact={ok}', flush=True)
import sys as _s
SKIP_LP={'reymont'}
for f in ['reymont','x-ray','dickens']:
    src=f'{S}/sil/f/{f}'; n=os.path.getsize(src)
    # lpaq1-h single stream vs parallel 2 MB blocks
    if f not in SKIP_LP:
     o=f'{W}/{f}.lph'; ct=t([sys.executable, BEST, LP, '6', src, o]); dt=t([LP,'d',o,o+'.out']); ok=filecmp.cmp(src,o+'.out',shallow=False); os.remove(o+'.out'); row(f,n,'lpaq1-h single',os.path.getsize(o),ct,dt,ok)
     o=f'{W}/{f}.lphp'; ct=t([sys.executable, PAR,'c',LP,'6','2',src,o]); dt=t([sys.executable, PAR,'d',LP,o,o+'.out']); ok=filecmp.cmp(src,o+'.out',shallow=False); os.remove(o+'.out'); row(f,n,'lpaq1-h parallel 2 MB',os.path.getsize(o),ct,dt,ok)
    # zpaq-hi one block (-m5) vs 2 MB blocks (-m51); decoded by STOCK zpaq with 2 threads
    for tag, m in (('zpaq-hi -m5 (one block)','5'),('zpaq-hi -m51 (2 MB blocks)','51')):
        d=f'{W}/{f}_z{m}'; os.makedirs(d, exist_ok=True); a=f'{d}/a.zpaq'
        if os.path.exists(a): os.remove(a)
        shutil.copy(src, f'{d}/{f}') if not os.path.exists(f'{d}/{f}') else None
        ct=t([ZH,'a',a,f,'-m'+m,'-threads','2'], cwd=d); x=f'{d}/x'; os.makedirs(x, exist_ok=True)
        dt=t([ZS,'x',a,'-to',x,'-threads','2','-force'], cwd=d); ok=filecmp.cmp(src,f'{x}/{f}',shallow=False); row(f,n,tag+' [stock decode]',os.path.getsize(a),ct,dt,ok)
