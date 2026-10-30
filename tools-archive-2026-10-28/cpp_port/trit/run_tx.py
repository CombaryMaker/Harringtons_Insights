import subprocess, os, sys, filecmp, json
from concurrent.futures import ThreadPoolExecutor
C='../../cant_dl/canterbury-corpus/canterbury/'
H='../v3/hcm3'
files=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
perms=['012','021','102','120','201','210']
def base(f):
    out=f'w/base_{f}.hcm'; subprocess.run([H,'c',C+f,out],capture_output=True); return os.path.getsize(out)
def job(args):
    m,p,f=args
    t=f'w/{m}{p}_{f}.t'; back=t+'.back'; z=t+'.hcm'
    subprocess.run(['./ttx','enc',m,p,C+f,t],check=True); subprocess.run(['./ttx','dec',m,p,t,back],check=True)
    ok=filecmp.cmp(C+f,back,shallow=False)
    subprocess.run([H,'c',t,z],capture_output=True)
    return dict(m=m,p=p,f=f,ok=ok,tsize=os.path.getsize(t),z=os.path.getsize(z))
with ThreadPoolExecutor(2) as ex:
    B=dict(zip(files,ex.map(base,files)))
    R=list(ex.map(job,[(m,p,f) for m in 'AB' for p in perms for f in files]))
json.dump(dict(B=B,R=R),open('tx.json','w'))
print('all inverses exact:', all(r['ok'] for r in R))
size={f:os.path.getsize(C+f) for f in files}
print('direct HCM v3 fast, all 11 (1M bits down to):', round(sum(B.values())/sum(size.values())*1e6))
for m in 'AB':
    for p in perms:
        rs=[r for r in R if r['m']==m and r['p']==p]
        tot=sum(r['z'] for r in rs); better=[r['f'] for r in rs if r['z']<B[r['f']]]
        print(f"method {m} swap {p}: all 11 -> {round(tot/sum(size.values())*1e6):,}  files better than direct: {better}")
