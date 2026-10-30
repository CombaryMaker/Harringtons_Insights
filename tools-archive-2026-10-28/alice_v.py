import sys, json, math, itertools
import numpy as np
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,S)
import hcm_v as H
data=open(S+'/cant_dl/canterbury-corpus/canterbury/alice29.txt','rb').read()
BASE=dict(tbits=22, match=True, verify=False, word=True, apm=True, apm_w=0.75, learn=True, lr=0.008, orders=(1,2,3,4,6), word2=False, limit_hi=255)
def run(cfg, d=data):
    H.CFG.clear(); H.CFG.update(BASE); H.CFG.update(cfg); return len(H.compress(d))
def best_codes(m, k):          # codes with the most 1s first, without listing all 2^m
    out=[]
    for ones in range(m, -1, -1):
        for zeros in itertools.combinations(range(m), m-ones):
            v=(1<<m)-1
            for z in zeros: v &= ~(1<<(m-1-z))
            out.append(v)
            if len(out)==k: return out
def partial_sort(d, m, k):     # your method 1: swap only the top-k m-bit patterns into the best codes
    bits=np.unpackbits(np.frombuffer(d,np.uint8)); L=len(bits)//m*m
    w=(1<<np.arange(m-1,-1,-1)).astype(np.int64); vals=bits[:L].reshape(-1,m).astype(np.int64)@w
    u,c=np.unique(vals,return_counts=True); F=[int(x) for x in u[np.argsort(-c,kind='stable')][:k]]
    B=best_codes(m,k); mp=dict(zip(F,B)); Fs,Bs=set(F),set(B)
    for s,t in zip(sorted(Bs-Fs), sorted(Fs-Bs)): mp[s]=t      # displaced codes take the freed slots
    out=vals.copy()
    for s,t in mp.items(): out[vals==s]=t
    ob=((out[:,None]>>np.arange(m-1,-1,-1))&1).astype(np.uint8).ravel()
    return np.packbits(np.concatenate((ob,bits[L:]))).tobytes(), math.ceil((k*m+8+32)/8)
job=sys.argv[1]
if job=='sort':
    for m,k in ((8,1),(8,2),(8,4),(16,4),(16,8),(16,16),(24,4),(24,8)):
        d2,extra=partial_sort(data,m,k); print(json.dumps(['Sort%d top%d'%(m,k), len(H.compress(d2)) if False else run({},d2)+extra]),flush=True)
else:
    for name,cfg in (('HCM (reference)',{}),('Verified matches + 90/10 blend',dict(verify=True,apm_w=0.9)),
                     ('+ order-5 context',dict(orders=(1,2,3,4,5,6))),('+ word-pair context',dict(word2=True)),
                     ('Orders 1,2,3,4,6,8',dict(orders=(1,2,3,4,6,8))),('Count limit 127',dict(limit_hi=127))):
        print(json.dumps([name, run(cfg)]),flush=True)
