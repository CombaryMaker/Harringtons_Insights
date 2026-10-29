import sys,subprocess as sp,numpy as np,json
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased'); from he3 import to_vals,targets,perm,apply
W='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bm/'; D='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased/'
m=json.load(open(W+'mut.json'))
F={'12bit':('synthetic_12bit_noise.bin',12),'bbits':('biased_bits_packed.bin',21),'bbytes':('nist/bin/biased-random-bytes.bin',16),'5bit':('synthetic_5bit_skewed.bin',5)}
def typed(v,J): return v.astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32)
for k,(f,J) in F.items():
    key,best=min(((a,b) for a,b in m[k].items() if a.startswith('renamesplit')),key=lambda x:x[1][0]); S=int(key.split('S')[1])
    d=open(D+f,'rb').read(); v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
    mp=perm(src,targets(J,S,len(src))); w=apply(v,mp); inv=np.arange(1<<J,dtype=np.uint32)
    for a,b in mp.items(): inv[b]=a
    hi=w>>np.uint64(J-S); lo=w&np.uint64((1<<(J-S))-1)
    typed(hi,S).tofile(W+'H'); typed(lo,J-S).tofile(W+'L'); inv.tofile(W+'I')
    (th,fh),(tl,fl)=best[2],best[3]
    o=sp.run([W+'mx',W+'H',str(th),str(fh),W+'L',str(tl),str(fl),W+'I',str(J),str(S),str(len(v)),D+f],capture_output=True,text=True).stdout.split()
    hdr=(len(src)*J+7)//8+16+(len(tail)+7)//8
    print(k,len(d),'S',S,'size',int(o[0])+int(o[1])+hdr,'roundtrip',o[2]=='1','decode_ms',o[3])
