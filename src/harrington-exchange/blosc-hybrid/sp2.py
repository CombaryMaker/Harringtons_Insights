# Speed search: renamed (fast-mode) streams and stock files under Blosc2 codecs x typesize x filter x blocksize.
import sys,subprocess as sp,numpy as np,itertools
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased'); from he3 import to_vals,targets,perm,apply
W='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/bm/'; D='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/biased/'
F={'12bit':('synthetic_12bit_noise.bin',[(12,4)]),'bbits':('biased_bits_packed.bin',[(21,14)]),'bbytes':('nist/bin/biased-random-bytes.bin',[(16,8),(8,4)]),'5bit':('synthetic_5bit_skewed.bin',[(5,3)])}
def grid(fn,tss):
    res=[]
    for ts,fl,cc,bs in itertools.product(tss,(0,1,2,3),(1,2,5),(0,262144,1048576)):
        o=sp.run([W+'bl2',fn,str(ts),str(fl),str(cc),str(bs)],capture_output=True,text=True).stdout.split()
        if o and int(o[0])>0 and o[1]=='1': res.append((int(o[0]),float(o[2]),ts,fl,cc,bs))
    return res
def front(res):
    res=sorted(res,key=lambda r:r[1]); f=[]; 
    for r in res:
        if not f or r[0]<f[-1][0]: f.append(r)
    return f
for k,(f,opts) in F.items():
    d=open(D+f,'rb').read(); stock=grid(D+f,[1,2,3,4]); sb=min(stock)
    print(k,'STOCK best size',sb,'| fastest',min(stock,key=lambda r:r[1]))
    for J,S in opts:
        v,tail=to_vals(d,J); u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]
        w=apply(v,perm(src,targets(J,S,len(src)))).astype(np.uint8 if J<=8 else np.uint16 if J<=16 else np.uint32); fn=W+'R'; w.tofile(fn)
        hdr=(len(src)*J+7)//8+16
        fr=front(grid(fn,sorted({1,2,4,w.itemsize})))
        print(' J',J,'S',S,'frontier (size+hdr, blosc ms, ts,fl,codec,bs):'); [print('   ',r[0]+hdr,r[1:]) for r in fr if r[0]+hdr<sb[0]+5000]
