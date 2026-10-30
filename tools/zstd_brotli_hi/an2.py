import json,sys,collections
rows=json.load(open(sys.argv[1]))
by=collections.defaultdict(lambda:collections.defaultdict(dict))
for lv,f,c,o,s,ms,ok,isb,isA,isB in rows:
    assert ok=='OK'
    by[lv][f][c]=(o,s,ms,isb,isA,isB)
for lv,fs in by.items():
    O=TB=TM=0; A=[0,0]; B=[0,0]; W=[0,0]
    for f,cs in fs.items():
        b=[v for v in cs.values() if v[3]][0]; O+=b[0]; TB+=b[1]; TM+=b[2]
        # re-apply rule with re-timed numbers; fallback to base if fails
        candA=[v for v in cs.values() if v[1]<b[1] and v[2]<=b[2]*1.03]
        a=min(candA,key=lambda v:v[1]) if candA else b
        candB=[v for v in cs.values() if v[1]<=b[1]]
        bb=min(candB,key=lambda v:v[2])
        A[0]+=a[1];A[1]+=a[2];B[0]+=bb[1];B[1]+=bb[2]
    pm=lambda x:round(x/O*1e6)
    print(f"{lv:5} stock {pm(TB):>7} {TM:7.3f}ms | smaller-nopain {pm(A[0]):>7} {A[1]:7.3f}ms ({100*(1-A[0]/TB):.2f}% smaller, decode x{TM/A[1]:.2f}) | faster-noLarger {pm(B[0]):>7} {B[1]:7.3f}ms (decode x{TM/B[1]:.2f})")
