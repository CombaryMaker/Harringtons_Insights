import subprocess, time, os, filecmp, shutil
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; W=S+'/parbench'; ZS=S+'/zpaqbin'
def t(cmd,cwd):
    a=time.perf_counter(); subprocess.run(cmd,capture_output=True,cwd=cwd); return time.perf_counter()-a
for f in ['reymont','x-ray','dickens']:
    src=f'{S}/sil/f/{f}'; n=os.path.getsize(src)
    for tag,m in (('stock zpaq -m5 (one block)','5'),('stock zpaq -m51 (2 MB blocks)','51')):
        d=f'{W}/{f}_s{m}'; shutil.rmtree(d,ignore_errors=True); os.makedirs(d); shutil.copy(src,f'{d}/{f}'); a=f'{d}/a.zpaq'
        ct=t([ZS,'a',a,f,'-m'+m,'-threads','2'],d); x=f'{d}/x'; os.makedirs(x)
        dt=t([ZS,'x',a,'-to',x,'-threads','2'],d); ok=filecmp.cmp(src,f'{x}/{f}',shallow=False); z=os.path.getsize(a)
        print(f'{f:9s} {tag:30s} {z:>10,} B  1M bits->{z/n*1e6:>8,.0f}  ratio {n/z:6.3f}  compress {ct:6.1f} s  decode {dt:6.1f} s  exact={ok}', flush=True)
