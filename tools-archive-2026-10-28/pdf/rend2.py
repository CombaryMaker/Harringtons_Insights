import subprocess,hashlib,time,statistics as st,os,resource
def lim(): resource.setrlimit(resource.RLIMIT_AS,(2_500_000_000,2_500_000_000))
done={l.split()[0] for l in open('rend.log')}
def md5(p):
    try:
        subprocess.run(['pdftoppm','-r','36','-l','3','-gray',p,'/tmp/rr'],capture_output=True,timeout=120,preexec_fn=lim)
    except subprocess.TimeoutExpired: return 'TO'
    h=hashlib.md5()
    for f in sorted(x for x in os.listdir('/tmp') if x.startswith('rr')):
        h.update(open('/tmp/'+f,'rb').read()); os.remove('/tmp/'+f)
    return h.hexdigest()
def t(p,n=3):
    ts=[]
    for _ in range(n):
        s=time.perf_counter()
        try: subprocess.run(['pdftoppm','-r','36','-l','3','-gray',p,'/tmp/tt'],capture_output=True,timeout=120,preexec_fn=lim)
        except subprocess.TimeoutExpired: return 999
        ts.append(time.perf_counter()-s)
    for f in os.listdir('/tmp'):
        if f.startswith('tt'): os.remove('/tmp/'+f)
    return st.median(ts)
L=[l.split() for l in open('all.log') if len(l.split())==5 and l.split()[3]!='None']
out=open('rend.log','a')
for b,o,q,h,n in L:
    if b in done or 'bomb' in b: continue
    ho,hq,hh=md5('pdfjs/test/pdfs/'+b),md5('q/'+b),md5('h/'+b)
    to,tq,th=t('pdfjs/test/pdfs/'+b),t('q/'+b),t('h/'+b)
    print(b,o,q,h,int(ho==hq),int(ho==hh),f'{to:.3f}',f'{tq:.3f}',f'{th:.3f}',file=out,flush=True)
