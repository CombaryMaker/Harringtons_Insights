import os,math
from collections import Counter
from var import sz,CMP,delta,C
from var2 import rec
def hbits(n,total=256):  # Harrington shrinking header: each entry needs ceil(log2(remaining)) bits
    return sum(math.ceil(math.log2(r)) for r in range(total,total-n,-1) if r>1)
def swapmap(b,x):  # top-x most common -> 0..x-1, everyone else keeps relative order in remaining slots
    c=Counter(b); top=sorted(range(256),key=lambda v:(-c[v],v))[:x]
    rest=[v for v in range(256) if v not in top]; order=top+rest; m=[0]*256
    for i,v in enumerate(order): m[v]=i
    return bytes(m[v] for v in b)
tot={c:[0,0,0] for c in CMP}
for fn in sorted(os.listdir(C)):
    if fn=='SHA1SUM': continue
    b=open(C+'/'+fn,'rb').read(); R=rec(b); line=[fn]
    for c in CMP:
        p=sz(c,b)
        dl=min(sz(c,delta(b,s))+2 for s in {1,R})            # delta only (no swap)
        bestsw,nm=10**9,''
        for x in (4,8,16,32,64,255):
            m=swapmap(b,x); hb=(hbits(x)+7)//8
            for s in {1,R}:
                r=sz(c,delta(m,s))+2+hb
                if r<bestsw: bestsw,nm=r,f'x={x} s={s} hdr={hb}B'
            r=sz(c,m)+1+hb
            if r<bestsw: bestsw,nm=r,f'x={x} no-delta hdr={hb}B'
        a=min(p,dl); s_=min(a,bestsw)
        tot[c][0]+=p; tot[c][1]+=a; tot[c][2]+=s_
        line.append(f'{c} plain {p} delta {dl} swap+ {bestsw} ({nm})')
    print(' | '.join(line),flush=True)
print('full 256 table header:',(hbits(255)+7)//8,'bytes vs 256 raw')
print(tot)
