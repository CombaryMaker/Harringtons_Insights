import subprocess,os,sys,time,statistics as st
sys.path.insert(0,'qx'); from var2 import rec
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
RAR=S+'/rarx/rar/rar'; UNRAR=S+'/rarx/rar/unrar'; C=S+'/cant_dl/canterbury-corpus/canterbury'; W=S+'/rarw'
FILES=[f for f in sorted(os.listdir(C)) if f!='SHA1SUM']
def make(f,opts,tag):
    a=f'{W}/{f}.{tag}.rar'
    if os.path.exists(a): os.remove(a)
    subprocess.run([RAR,'a','-idq','-ep','-m5','-ma5']+opts+[a,C+'/'+f],check=True,capture_output=True)
    return a, os.path.getsize(a)
def dtime(a,n=15):
    t=[]
    for _ in range(n):
        s=time.perf_counter(); subprocess.run([UNRAR,'p','-inul',a],stdout=subprocess.DEVNULL); t.append(time.perf_counter()-s)
    return st.median(t)
def ok(a,f): return subprocess.run([UNRAR,'p','-inul',a],capture_output=True).stdout==open(C+'/'+f,'rb').read()
T=[0,0]; TD=[0,0]
for f in FILES:
    R=rec(open(C+'/'+f,'rb').read())
    base,bs=make(f,[],'base')
    cands={'x+':['-mcx+'],'l+':['-mcl+'],'xl+':['-mcx+','-mcl+'],'mc-':['-mc-'],f'D{R}':[f'-mc{R}d+'],f'D{R}x':[f'-mc{R}d+','-mcx+'],'D1':['-mc1d+'],'D2':['-mc2d+']}
    best,bn,bsz=base,'base',bs
    for n,o in cands.items():
        a,s=make(f,o,n.replace('+','p'))
        if s<bsz and ok(a,f): best,bn,bsz=a,n,s
    tb=dtime(base); tn=dtime(best) if best!=base else tb
    if tn>tb*1.0 and best!=base:   # no-pain: must not decode slower
        tb2=dtime(base); tn2=dtime(best)
        if tn2>tb2: best,bn,bsz,tn=base,'base',bs,tb
    T[0]+=bs;T[1]+=bsz;TD[0]+=tb;TD[1]+=tn
    print(f'{f} R={R} rar -m5 {bs} -> {bsz} ({bn}) decode {tb*1e3:.1f} -> {tn*1e3:.1f} ms',flush=True)
print('TOTAL',T,'decode ms',[round(x*1e3,1) for x in TD])
