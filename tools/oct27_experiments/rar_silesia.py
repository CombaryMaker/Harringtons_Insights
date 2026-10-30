import subprocess,os,sys,time,statistics as st
sys.path.insert(0,'qx'); from var2 import rec
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
RAR=S+'/rarx/rar/rar'; UNRAR=S+'/rarx/rar/unrar'; D=S+'/sil/f'; W=S+'/rarw/sil'; os.makedirs(W,exist_ok=True)
f=sys.argv[1]; src=D+'/'+f; data=open(src,'rb').read(); R=rec(data); U=len(data)
def make(opts,tag):
    a=f'{W}/{f}.{tag}.rar'
    if os.path.exists(a): os.remove(a)
    subprocess.run([RAR,'a','-idq','-ep','-m5','-ma5','-mt1']+opts+[a,src],check=True,capture_output=True); return a
def dt(a,n=9):
    t=[]
    for _ in range(n):
        s=time.perf_counter(); subprocess.run([UNRAR,'p','-inul','-mt1',a],stdout=subprocess.DEVNULL); t.append(time.perf_counter()-s)
    return st.median(t)
def ok(a): return subprocess.run([UNRAR,'p','-inul',a],capture_output=True).stdout==data
C={'base':[],'x+':['-mcx+'],'l+':['-mcl+'],'d-':['-mcd-'],'D1':['-mc1d+'],'D2':['-mc2d+'],'D4':['-mc4d+'],f'D{R}':[f'-mc{R}d+'],'D2x':['-mc2d+','-mcx+'],f'D{R}x':[f'-mc{R}d+','-mcx+'],'e+':['-mce+'],'e-':['-mce-'],'e+x':['-mce+','-mcx+']}
print(f,'detected record',R,flush=True)
res={}
for n,o in C.items():
    a=make(o,n.replace('+','p')); res[n]=(a,os.path.getsize(a)); print(n,res[n][1],flush=True)
b=res['base']; tb=st.median([dt(b[0]) for _ in range(3)])
order=sorted([k for k in res if k!='base' and res[k][1]<b[1]],key=lambda k:res[k][1])[:4]
win=None
for k in order:
    assert ok(res[k][0])
    tk=st.median([dt(res[k][0]) for _ in range(3)])
    print(f'  try {k} {res[k][1]} decode {tk*1e3:.1f} vs base {tb*1e3:.1f}',flush=True)
    if tk<=tb: win=(k,tk); break
if win: k,tk=win; print(f'RESULT {f} {U} base {b[1]} {tb*1e3:.1f} -> {k} {res[k][1]} {tk*1e3:.1f}')
else: print(f'RESULT {f} {U} base {b[1]} {tb*1e3:.1f} -> base {b[1]} {tb*1e3:.1f}')
