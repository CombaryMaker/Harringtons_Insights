import sys,collections
fn,key=sys.argv[1],sys.argv[2]   # key: 'L' or 'q'
R=collections.defaultdict(dict)   # (level)->{cfg:{file:(orig,comp,ms)}}
for l in open(fn):
    p=l.split()
    if p[0]!='RES': continue
    cfg,f,o,c,ms=p[1],p[2],int(p[3]),int(p[4]),float(p[5])
    lv=cfg.split(',')[0]
    R[lv].setdefault(cfg,{})[f]=(o,c,ms)
out={}
for lv,cf in R.items():
    base=cf[lv if key=='L' else lv+',w=24']
    files=list(base)
    A={};B={}
    for f in files:
        bo,bc,bm=base[f]
        cand=[(v[f][1],v[f][2],c) for c,v in cf.items()]
        okA=[x for x in cand if x[0]<bc and x[1]<=bm*1.03]
        A[f]=min(okA) if okA else (bc,bm,'base')
        okB=[x for x in cand if x[0]<=bc]
        B[f]=min(okB,key=lambda x:x[1]) if okB else (bc,bm,'base')
    O=sum(base[f][0] for f in files)
    tb=sum(base[f][1] for f in files); tm=sum(base[f][2] for f in files)
    ta=sum(A[f][0] for f in files); tam=sum(A[f][1] for f in files)
    tbb=sum(B[f][0] for f in files); tbm=sum(B[f][1] for f in files)
    pm=lambda x: round(x/O*1e6)
    print(f"{lv:5} base {pm(tb):>7} {tm:8.3f}ms | smallest-nopain {pm(ta):>7} {tam:8.3f}ms ({100*(1-ta/tb):.2f}% smaller, x{tm/tam:.2f}) | fastest-noLarger {pm(tbb):>7} {tbm:8.3f}ms (x{tm/tbm:.2f})")
    out[lv]=(A,B)
import json; json.dump({lv:{'A':{f:v[2] for f,v in A.items()},'B':{f:v[2] for f,v in B.items()}} for lv,(A,B) in out.items()},open(fn+'.pick.json','w'),indent=0)
