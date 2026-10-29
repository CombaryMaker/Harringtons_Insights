# Harrington Exchange at arbitrary bit widths W: swap (top-K frequency map, completed to a permutation) + juggle split at j bits.
import numpy as np, subprocess, sys, json, os
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; Z=S+'/z3/zstd/programs/zstd'
LV=os.environ.get('LV','-3')
def zs(b): return len(subprocess.run([Z,'-q',LV,'-c'],input=b,capture_output=True).stdout)
def chunks(data,W):
    bits=np.unpackbits(np.frombuffer(data,np.uint8)); n=len(bits)//W; pad=len(bits)-n*W
    m=bits[:n*W].reshape(n,W); vals=np.zeros(n,np.uint64)
    for k in range(W): vals=(vals<<np.uint64(1))|m[:,k].astype(np.uint64)
    return vals,bits[n*W:]
def tobits(vals,W):
    return ((vals[:,None]>>np.arange(W-1,-1,-1,dtype=np.uint64))&np.uint64(1)).astype(np.uint8)
def targets(W,j,K):
    # most zeros in the first j bits, then most zeros overall; enumerate only the K best patterns
    out=[]; 
    for zf in range(j,-1,-1):                      # zeros in first j bits, descending
        for za in range(W,-1,-1):
            pass
    # cheap generator for K patterns: popcount-ordered within leading-zero classes, small K only
    import itertools
    cand=[]
    for ones_hi in range(0,j+1):
        for hi in itertools.combinations(range(j),ones_hi):
            for ones_lo in range(0,W-j+1):
                for lo in itertools.combinations(range(W-j),ones_lo):
                    v=0
                    for b in hi: v|=1<<(W-1-b)
                    for b in lo: v|=1<<(W-j-1-b)
                    cand.append(v)
                    if len(cand)>=K: return cand
                if len(cand)>=K: return cand
    return cand
def he(data,W,j,K):
    vals,tail=chunks(data,W)
    u,c=np.unique(vals,return_counts=True); order=np.argsort(-c,kind='stable'); src=u[order][:K]
    K=len(src); tg=np.array(targets(W,j,K),np.uint64)[:K]
    # permutation: src->tg; displaced values (tg not in src) -> (src not in tg), both sorted
    s_set=set(src.tolist()); t_set=set(tg.tolist())
    a=sorted(t_set-s_set); b=sorted(s_set-t_set)
    mp=dict(zip(src.tolist(),tg.tolist())); mp.update(zip(a,b))
    keys=np.array(list(mp.keys()),np.uint64); valsm=np.array(list(mp.values()),np.uint64)
    idx=np.searchsorted(np.sort(keys),vals); sk=np.argsort(keys); ks=keys[sk]; vs=valsm[sk]
    pos=np.searchsorted(ks,vals); hit=(pos<len(ks))&(ks[np.minimum(pos,len(ks)-1)]==vals)
    out=vals.copy(); out[hit]=vs[pos[hit]]
    b=tobits(out,W)
    def aligned(part):   # each chunk's part right-aligned in whole bytes
        p=part.shape[1]; nb=(p+7)//8; pad=np.zeros((part.shape[0],nb*8-p),np.uint8)
        return np.packbits(np.hstack([pad,part]),axis=1).tobytes()
    hi=b[:,:j]; lo=b[:,j:][::-1]
    t=min(zs(np.packbits(hi.ravel()).tobytes())+zs(np.packbits(lo.ravel()).tobytes()), zs(aligned(hi))+zs(aligned(lo)))
    hdr=(2*K*W+7)//8+4
    return t+hdr+len(tail)//8+1
f,lab=sys.argv[1],sys.argv[2]; data=open(f,'rb').read()[:int(sys.argv[3])] if len(sys.argv)>3 else open(f,'rb').read()
base=zs(data); rows=[]
for sw in range(1,24):                 # swap part (leading bits) = sw; juggle chunk = jg > sw, up to 24
    for jg in range(sw+1,25):
        try: rows.append((he(data,jg,sw,256),sw,jg))
        except Exception as e: print('skip',sw,jg,e)
rows.sort()
fin=[]
os.environ['LV']='-19'
LV='-19'
b19=zs(data)
for s_,sw,jg in rows[:3]: fin.append((he(data,jg,sw,256),sw,jg))
print(f"{lab}: {len(data):,} B | zstd-3 plain {base:,}, best 3 (scored at -3): "+'; '.join(f'swap {sw}/juggle {jg} {s_:,} ({(s_-base)/base*100:+.1f}%)' for s_,sw,jg in rows[:3]))
xz=len(subprocess.run(['xz','-9e','-c'],input=data,capture_output=True).stdout)
print(f"   xz -9e plain {xz:,} | at zstd -19: plain {b19:,}; "+'; '.join(f'swap {sw}/juggle {jg} {s_:,} ({(s_-b19)/b19*100:+.1f}%)' for s_,sw,jg in fin),flush=True)
json.dump(dict(file=lab,n=len(data),base3=base,base19=b19,top=rows[:30],final19=fin),open(f'he2_{lab}.json','w'))
