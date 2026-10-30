import os,subprocess,sys
sys.path.insert(0,'../qx'); from var2 import rec
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sets={'Canterbury':S+'/cant_dl/canterbury-corpus/canterbury','Silesia':S+'/sil/f'}
for name,D in sets.items():
    T=[0,0,0,0,0];print('==',name)
    for f in sorted(os.listdir(D)):
        if f=='SHA1SUM': continue
        p=D+'/'+f; R=max(rec(open(p,'rb').read()),2)
        U,ps,pt,ds,dt,as_,at=[float(x) for x in subprocess.run(['./sb',p,str(R),'31' if os.path.getsize(p)<2e6 else '9'],capture_output=True,text=True).stdout.split()]
        # no-pain pick: smallest variant that decodes no slower than plain
        best=('plain',ps,pt)
        for n,s,t in (('delta',ds,dt),('activity',as_,at)):
            if s<best[1] and t<=pt: best=(n,s,t)
        m=lambda x:round(x/U*1e6)
        print(f'{f} R={R}: plain {m(ps)} ({pt:.0f}us) delta {m(ds)} ({dt:.0f}us) activity {m(as_)} ({at:.0f}us) -> {best[0]}',flush=True)
        T[0]+=U;T[1]+=ps;T[2]+=best[1];T[3]+=pt;T[4]+=best[2]
    print(f'TOTAL {name}: {round(T[1]/T[0]*1e6)} -> {round(T[2]/T[0]*1e6)} per million, decode {T[3]/1e3:.1f} -> {T[4]/1e3:.1f} ms')
