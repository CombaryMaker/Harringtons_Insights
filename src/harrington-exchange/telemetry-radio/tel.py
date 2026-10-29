# Telemetry benchmark: zstd -19, xz -9e, Blosc2 (best), ZPAQ -m5, Harrington Exchange (limited S/J sweep, zstd -19), Exchange->Blosc2 hybrid (fast mode).
import sys,glob,os,lzma,subprocess as sp,numpy as np,itertools,json
S_='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0,S_+'/biased'); from he3 import encode,decode,zc,to_vals,targets,perm,apply
BL=S_+'/bm/bl2'; ZPAQ=S_+'/zpaqbin'; W=S_+'/tel/'
def blbest(fn,tss):
    b=None
    for ts,fl,cc,bs in itertools.product(tss,(0,1,2,3),(5,),(0,1048576)):
        o=sp.run([BL,fn,str(ts),str(fl),str(cc),str(bs)],capture_output=True,text=True).stdout.split()
        if o and int(o[0])>0 and o[1]=='1' and (b is None or int(o[0])<b[0]): b=(int(o[0]),float(o[2]),ts,fl,bs)
    return b
def zpaq(fn):
    sp.run(f'cd {W} && rm -f z.zpaq && cp {fn} zin && {ZPAQ} a z.zpaq zin -m5 >/dev/null 2>&1',shell=True); return os.path.getsize(W+'z.zpaq')
out={}
for f in sorted(glob.glob(W+'*.bin')):
    d=open(f,'rb').read(); k=os.path.basename(f)
    r={'bytes':len(d),'zstd19':len(zc(d,19)),'xz9e':len(lzma.compress(d,preset=9|lzma.PRESET_EXTREME)),'blosc2':blbest(f,[1,2,4,8]),'zpaq_m5':zpaq(f)}
    best=None
    for J in (8,10,12,14,16,20,24):
        for S in sorted({max(1,J//4),J//2,(3*J)//4}):
            for m in ('full','split'):
                e=encode(d,J,S,mode=m,lay=0,level=19)
                if best is None or len(e)<best[0]: best=(len(e),J,S,m)
    e=encode(d,best[1],best[2],mode=best[3],lay=0,level=19); assert decode(e)==d
    r['exchange']=best
    J,S=best[1],best[2]; v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
    w=apply(v,perm(src,targets(J,S,len(src)))).astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32); w.tofile(W+'R.raw')
    hb=blbest(W+'R.raw',sorted({1,2,4,w.itemsize})); r['hybrid_fast']=(hb[0]+(len(src)*J+7)//8+16,)+hb[1:]
    out[k]=r; print(k,json.dumps(r),flush=True)
json.dump(out,open(W+'tel.json','w'),indent=1)
