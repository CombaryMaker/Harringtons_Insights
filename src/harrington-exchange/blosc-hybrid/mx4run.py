# Exchange->Blosc2 mutant v4: searches per-stream Blosc2 settings, then times full fused decode (median of 21) for fast and max modes.
import sys,subprocess as sp,numpy as np,itertools
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased'); from he3 import to_vals,targets,perm,apply
W='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bm/'; D='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased/'
F={'12bit':('synthetic_12bit_noise.bin',12,[4],[8]),'bbits':('biased_bits_packed.bin',21,[14],[14]),'bbytes':('nist/bin/biased-random-bytes.bin',16,[8],[8]),'bbytes8':('nist/bin/biased-random-bytes.bin',8,[4],[]),'5bit':('synthetic_5bit_skewed.bin',5,[3],[4])}
def ty(v,J): return v.astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32)
def best(fn,item,slack):
    res=[]
    for ts,fl,cc,bs in itertools.product(sorted({1,2,4,item}),(0,1,2,3),(1,2,5),(0,262144,1048576)):
        o=sp.run([W+'bl2',fn,str(ts),str(fl),str(cc),str(bs)],capture_output=True,text=True).stdout.split()
        if o and int(o[0])>0 and o[1]=='1': res.append((int(o[0]),float(o[2]),ts,fl,cc,bs))
    m=min(r[0] for r in res); return min([r for r in res if r[0]<=m+slack],key=lambda r:r[1])
for k,(f,J,Sf,Sm) in F.items():
  d=open(D+f,'rb').read(); v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
  hdr=(len(src)*J+7)//8+16+(len(tail)+7)//8
  for mode,Ss in (('fast',Sf),('max',Sm)):
   for S in Ss:
    for slack in (300,):
      mp=perm(src,targets(J,S,len(src))); w=apply(v,mp); inv=np.arange(1<<J,dtype=np.uint32)
      for a,b in mp.items(): inv[b]=a
      inv.tofile(W+'I')
      if mode=='fast':
        ty(w,J).tofile(W+'H'); b1=best(W+'H',ty(w,J).itemsize,slack); args=[W+'H']+[str(x) for x in b1[2:]]; Sx=0
      else:
        hi=ty(w>>np.uint64(J-S),S); lo=ty(w&np.uint64((1<<(J-S))-1),J-S); hi.tofile(W+'H'); lo.tofile(W+'L')
        b1=best(W+'H',hi.itemsize,slack); b2=best(W+'L',lo.itemsize,slack); args=[W+'H']+[str(x) for x in b1[2:]]+[W+'L']+[str(x) for x in b2[2:]]; Sx=S
      o=sp.run([W+'mx4',str(J),str(Sx),str(len(v)),W+'I',D+f]+args,capture_output=True,text=True).stdout.split()
      print(k,mode,'S',S,'slack',slack,'size',int(o[0])+int(o[1])+hdr,'ok',o[2]=='1','decode_ms',o[3],flush=True)
