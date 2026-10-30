import subprocess,hashlib,time,statistics as st,os,sys
def render(p):
    r=subprocess.run(['pdftoppm','-r','36','-l','3','-gray',p],capture_output=True,timeout=300)
    return hashlib.md5(r.stdout).hexdigest(), r.returncode
def t(p,n=3):
    ts=[]
    for _ in range(n):
        s=time.perf_counter(); subprocess.run(['pdftoppm','-r','36','-l','3','-gray',p],capture_output=True,timeout=300); ts.append(time.perf_counter()-s)
    return st.median(ts)
L=[l.split() for l in open('all.log') if len(l.split())==5 and l.split()[3]!='None']
out=open('rend.log','w')
for b,o,q,h,n in L:
    try:
        ho,_=render('pdfjs/test/pdfs/'+b); hq,_=render('q/'+b); hh,_=render('h/'+b)
        to,tq,th=t('pdfjs/test/pdfs/'+b),t('q/'+b),t('h/'+b)
    except Exception as e:
        print(b,'ERR',e,file=out,flush=True); continue
    print(b,o,q,h,int(ho==hq),int(ho==hh),f'{to:.3f}',f'{tq:.3f}',f'{th:.3f}',file=out,flush=True)
