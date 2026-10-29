# Blosc2 + Harrington Exchange hybrids: unpack J-bit symbols to typed ints, optionally rename (Exchange map), optionally split (S), then Blosc2.
import sys,subprocess as sp,numpy as np,itertools,json
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased'); from he3 import to_vals,targets,perm,apply
D='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased/'; BL='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bl/run/bl'
F={'12bit':('synthetic_12bit_noise.bin',12),'bbits':('biased_bits_packed.bin',21),'bbytes':('nist/bin/biased-random-bytes.bin',16),'5bit':('synthetic_5bit_skewed.bin',5)}
def blbest(arr,tag):
    fn=f'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bm/{tag}.raw'; open(fn,'wb').write(arr.tobytes()); best=None
    for ts in sorted({1,2,4,arr.itemsize}):
        for fl in (0,1,2,3):
            o=sp.run([BL,fn,str(ts),str(fl)],capture_output=True,text=True).stdout.split()
            if o and int(o[0])>0 and o[1]=='1' and (best is None or int(o[0])<best[0]): best=(int(o[0]),float(o[2]),ts,fl)
    return best
def typed(v,J): return v.astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32)
out={}
for k,(f,J) in F.items():
    d=open(D+f,'rb').read(); v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
    hdr=(len(src)*J+7)//8+16
    r={'unpack':blbest(typed(v,J),k+'u')}
    for S in sorted({1,2,J//4 or 1,J//3 or 1,J//2,(2*J)//3,J-1}):
        if not 1<=S<J: continue
        mp=perm(src,targets(J,S,len(src))); w=apply(v,mp)
        b=blbest(typed(w,J),f'{k}r{S}'); r[f'rename_S{S}']=(b[0]+hdr,)+b[1:]
        hi=(w>>np.uint64(J-S)); lo=w&np.uint64((1<<(J-S))-1)
        b1=blbest(typed(hi,S),f'{k}h{S}'); b2=blbest(typed(lo,J-S),f'{k}l{S}')
        r[f'renamesplit_S{S}']=(b1[0]+b2[0]+hdr,b1[1]+b2[1],(b1[2],b1[3]),(b2[2],b2[3]))
    out[k]=r; best=min(r.items(),key=lambda x:x[1][0]); print(k,len(d),'unpack',r['unpack'],'BEST',best,flush=True)
json.dump(out,open('mut.json','w'),indent=1)
