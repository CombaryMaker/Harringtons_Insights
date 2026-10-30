import os,subprocess
from var import sz,CMP,nibsplit,freqrank,delta,C
def trits(b,flip=0):
    d=[[] for _ in range(6)]
    for x in b:
        x^=flip
        for k in range(6): d[k].append(x%3); x//=3
    return d
def pack5(t):  # 5 trits per byte (243 values)
    t=t+[0]*(-len(t)%5); return bytes(t[i]+3*t[i+1]+9*t[i+2]+27*t[i+3]+81*t[i+4] for i in range(0,len(t),5))
def tri_bytes(b,f): d=trits(b,f); return [bytes(v for i in range(len(b)) for v in [d[k][i] for k in range(6)])]
def tri_planes(b,f): return [pack5(p) for p in trits(b,f)]
def tri_planes_raw(b,f): return [bytes(p) for p in trits(b,f)]
def rec(b):  # HCM record-length detector: byte repeating at equal spacing 3+ times
    from collections import Counter
    c=Counter()
    last={}
    for i,x in enumerate(b[:200000]):
        if x in last: c[i-last[x]]+=1
        last[x]=i
    for s,_ in c.most_common(20):
        if 2<=s<=512: return s
    return 1
V={}
for f in (0,255):
    V[f'trit-bytes f{f}']=lambda b,f=f:tri_bytes(b,f)
    V[f'trit-planes5 f{f}']=lambda b,f=f:tri_planes(b,f)
    V[f'trit-planes f{f}']=lambda b,f=f:tri_planes_raw(b,f)
V['record-delta']=lambda b:[delta(b,rec(b))]
V['record-juggle']=lambda b:[b[i::rec(b)] for i in range(rec(b))]
V['record-juggle+nibble-juggle']=lambda b:[s for i in range(rec(b)) for s in nibsplit(b[i::rec(b)])]
V['record-delta+nibble-juggle']=lambda b:nibsplit(delta(b,rec(b)))
def _main():
    tot={c:[0,0] for c in CMP}
    for fn in sorted(os.listdir(C)):
        if fn=='SHA1SUM': continue
        b=open(C+'/'+fn,'rb').read(); line=[fn+f' rec={rec(b)}']
        for c in CMP:
            p=sz(c,b); best,bn=p+1,'plain'
            for n,g in V.items():
                s=g(b); r=sum(sz(c,x) for x in s)+1+4*(len(s)-1)
                if r<best: best,bn=r,n
            tot[c][0]+=p; tot[c][1]+=best; line.append(f'{c} {p}->{best} {bn}')
        print(' | '.join(line),flush=True)
    print(tot)
    
if __name__=="__main__": _main()
