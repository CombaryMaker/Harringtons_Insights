import subprocess, sys, os, time, resource, json, shutil
SRC='cant_dl/canterbury-corpus/canterbury'
tool=sys.argv[1]; files=sys.argv[2:]
P='cmp_src'
os.makedirs('cmp_out',exist_ok=True)
for f in files:
    src=os.path.join(SRC,f); out=os.path.abspath(f'cmp_out/{f}.{tool}'); back=out+'.back'
    for p in (out,back):
        if os.path.exists(p): os.remove(p)
    if tool=='paq8':
        enc=[os.path.abspath(P+'/paq8px/b/paq8px'),'-8',os.path.abspath(src),out]
    elif tool=='paq8L':
        enc=[os.path.abspath(P+'/paq8px/b/paq8px'),'-8L',os.path.abspath(src),out]
    elif tool=='cmix':
        enc=[os.path.abspath(P+'/cmix/cmix'),'-c',os.path.abspath(src),out]
    t=time.time(); r=subprocess.run(enc,capture_output=True,text=True,cwd=P+('/cmix' if tool=='cmix' else '/paq8px')); te=time.time()-t
    ok=None; td=None
    if r.returncode==0 and os.path.exists(out):
        if tool.startswith('paq8'):
            dec=[os.path.abspath(P+'/paq8px/b/paq8px'),'-d',out,back]
        else:
            dec=[os.path.abspath(P+'/cmix/cmix'),'-d',out,back]
        t=time.time(); r2=subprocess.run(dec,capture_output=True,text=True,cwd=P+('/cmix' if tool=='cmix' else '/paq8px')); td=time.time()-t
        ok = os.path.exists(back) and open(back,'rb').read()==open(src,'rb').read()
    print(json.dumps({"file":f,"tool":tool,"size":os.path.getsize(src),"out":os.path.getsize(out) if os.path.exists(out) else None,"rc":r.returncode,"enc":round(te,1),"dec":round(td,1) if td else None,"ok":ok,"err":r.stderr[-300:] if r.returncode else ""}),flush=True)
