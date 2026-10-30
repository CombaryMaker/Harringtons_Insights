import os,glob,subprocess,sys
from hi import run
D='pdfjs/test/pdfs'
for f in sorted(glob.glob(D+'/*.pdf')):
    s=os.path.getsize(f)
    if s<100000: continue
    b=os.path.basename(f); q='q/'+b
    if not os.path.exists(q): continue
    try: n=run(f,'h/'+b,'w_'+str(os.getpid()))
    except Exception as e: n=None
    hs=os.path.getsize('h/'+b) if n is not None and os.path.exists('h/'+b) else None
    print(b,s,os.path.getsize(q),hs,n,flush=True)
