# Runs Exchange->Blosc2 mutant (fast and max modes) with full timed decode and round-trip check.
import sys,subprocess as sp,numpy as np,json
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased'); from he3 import to_vals,targets,perm,apply
W='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bm/'; D='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased/'
m=json.load(open(W+'mut.json'))
F={'12bit':('synthetic_12bit_noise.bin',12),'bbits':('biased_bits_packed.bin',21),'bbytes':('nist/bin/biased-random-bytes.bin',16),'5bit':('synthetic_5bit_skewed.bin',5)}
def typed(v,J): return v.astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32)
for k,(f,J) in F.items():
  d=open(D+f,'rb').read(); v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
  for mode in ('rename','renamesplit'):
    key,best=min(((a,b) for a,b in m[k].items() if a.startswith(mode+'_')),key=lambda x:x[1][0]); S=int(key.split('S')[1])
    mp=perm(src,targets(J,S,len(src))); w=apply(v,mp); inv=np.arange(1<<J,dtype=np.uint32)
    for a,b in mp.items(): inv[b]=a
    inv.tofile(W+'I'); hdr=(len(src)*J+7)//8+16+(len(tail)+7)//8
    if mode=='rename': typed(w,J).tofile(W+'H'); args=[W+'H',str(best[2]),str(best[3])]; Sx=0
    else:
      typed(w>>np.uint64(J-S),S).tofile(W+'H'); typed(w&np.uint64((1<<(J-S))-1),J-S).tofile(W+'L'); (th,fh),(tl,fl)=best[2],best[3]
      args=[W+'H',str(th),str(fh),W+'L',str(tl),str(fl)]; Sx=S
    o=sp.run([W+'mx3',str(J),str(Sx),str(len(v)),W+'I',D+f]+args,capture_output=True,text=True).stdout.split()
    print(k,len(d),'fast' if mode=='rename' else 'max ','S',S,'size',int(o[0])+int(o[1])+hdr,'roundtrip',o[2]=='1','decode_ms',o[3],flush=True)
