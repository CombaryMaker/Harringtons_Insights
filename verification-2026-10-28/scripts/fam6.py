from cli import *
import numpy as np,time,statistics as st,subprocess,json
C=CT+'kennedy.xls'; d=open(C,'rb').read(); a=np.frombuffer(d,np.uint8); U=len(d)
def targets(j):
    zf=lambda v: j-bin(v>>(8-j)).count('1'); za=lambda v: 8-bin(v).count('1')
    return sorted(range(256),key=lambda v:(-zf(v),-za(v),v))
def he_enc(a,j):
    cnt=np.bincount(a,minlength=256); src=sorted(range(256),key=lambda v:(-cnt[v],v)); tg=targets(j)
    m=np.zeros(256,np.uint8)
    for s,t in zip(src,tg): m[s]=t
    b=m[a]; hi=(b>>(8-j)).astype(np.uint8); lo=(b&((1<<(8-j))-1)).astype(np.uint8)[::-1].copy()
    return m,hi,lo
def he_dec(m,hi,lo,j):
    inv=np.zeros(256,np.uint8); inv[m]=np.arange(256,dtype=np.uint8)
    b=(hi.astype(np.uint8)<<(8-j))|lo[::-1]; return inv[b]
def packbits(sym,w): return np.packbits(np.unpackbits(sym[:,None],axis=1)[:,8-w:].ravel()).tobytes()
def unpack(buf,w,n): bits=np.unpackbits(np.frombuffer(buf,np.uint8))[:n*w].reshape(n,w); return np.packbits(np.hstack([np.zeros((n,8-w),np.uint8),bits]),axis=1).ravel()
TOOL={'zstd -22':([Z,'-q','--ultra','-22','-c'],[Z,'-q','-dc']),'lz4 -12':([S+'/z3/lz4/lz4','-q','-12','-c'],[S+'/z3/lz4/lz4','-q','-dc'])}
def comp(b,t): return subprocess.run(TOOL[t][0],input=b,capture_output=True).stdout
def decomp(b,t): return subprocess.run(TOOL[t][1],input=b,capture_output=True).stdout
for t in ('lz4 -12','zstd -22'):
    # choose best j/layout by size as in the original test
    best=None
    for j in (1,2,3,4):
        m,hi,lo=he_enc(a,j)
        for lay in ('tight','bytewise'):
            s1=packbits(hi,j) if lay=='tight' else hi.tobytes(); s2=packbits(lo,8-j) if lay=='tight' else lo.tobytes()
            n=len(comp(s1,t))+len(comp(s2,t))+256
            if best is None or n<best[0]: best=(n,j,lay)
    _,j,lay=best
    def enc():
        m,hi,lo=he_enc(a,j); s1=packbits(hi,j) if lay=='tight' else hi.tobytes(); s2=packbits(lo,8-j) if lay=='tight' else lo.tobytes()
        return m,comp(s1,t),comp(s2,t)
    E=[];P=[]
    for _ in range(3):
        t0=time.perf_counter(); m,c1,c2=enc(); E.append(time.perf_counter()-t0)
        t0=time.perf_counter(); pc=comp(d,t); P.append(time.perf_counter()-t0)
    D=[];DP=[]
    for _ in range(9):
        t0=time.perf_counter(); s1=decomp(c1,t); s2=decomp(c2,t)
        hi=unpack(s1,j,U) if lay=='tight' else np.frombuffer(s1,np.uint8); lo=unpack(s2,8-j,U) if lay=='tight' else np.frombuffer(s2,np.uint8)
        out=he_dec(m,hi,lo,j); D.append(time.perf_counter()-t0)
        t0=time.perf_counter(); po=decomp(pc,t); DP.append(time.perf_counter()-t0)
    ok=out.tobytes()==d and po==d
    base=dict(size=len(pc),enc_s=st.median(P),dec_s=st.median(DP),ok=po==d)
    cand=dict(size=len(c1)+len(c2)+256,enc_s=st.median(E),dec_s=st.median(D),ok=ok,cfg=f'j={j} {lay}')
    log('Harrington Exchange + juggle',f'kennedy.xls|{t}',U,base,cand,dict(note='prototype in Python/numpy + CLI compressor via pipes (stdin, as in Table 15.1); times are whole-pipeline wall clock; decoder implemented here for the first time to verify losslessness'))
# ---- Harrington shrinking header (256-entry permutation)
import math,random
def hdr_enc(perm):
    rem=list(range(256)); bits=[]
    for v in perm:
        k=len(rem); w=math.ceil(math.log2(k)) if k>1 else 0; i=rem.index(v); rem.pop(i)
        bits.append((i,w))
    out=0; nb=0
    for i,w in bits: out=(out<<w)|i; nb+=w
    return out,nb
def hdr_dec(val,nb):
    rem=list(range(256)); ws=[math.ceil(math.log2(k)) if k>1 else 0 for k in range(256,0,-1)]; perm=[]; pos=nb
    for w in ws:
        pos-=0
    # read from the most significant end
    shift=nb
    for k,w in zip(range(256,0,-1),ws):
        shift-=w; i=(val>>shift)&((1<<w)-1) if w else 0; perm.append(rem.pop(i))
    return perm
m,_,_=he_enc(a,2); perm=list(m)
rs=[]
for p,label in ((perm,'kennedy.xls Harrington Exchange table (j=2)'),(random.Random(2360932695).sample(range(256),256),'random permutation (seed 2360932695)')):
    E=[];D=[]
    for _ in range(21):
        t0=time.perf_counter(); v,nb=hdr_enc(p); E.append(time.perf_counter()-t0)
        t0=time.perf_counter(); q=hdr_dec(v,nb); D.append(time.perf_counter()-t0)
    r=dict(family='Harrington shrinking header',sample=label,raw_bytes=256,header_bits=nb,header_bytes=math.ceil(nb/8),enc_s=st.median(E),dec_s=st.median(D),ok=q==[int(x) for x in p])
    open(OUT,'a').write(json.dumps(r)+'\n'); print(r)
