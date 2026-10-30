import os,re,time,subprocess,glob,numpy as np,json
from PIL import Image
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
J2=S+'/rv/src/jbig2enc/src/jbig2'; J2D=S+'/rv/src/jbig2dec/jbig2dec'; Z=S+'/z3/zstd/programs/zstd'
def tm(cmd): t=time.perf_counter(); r=subprocess.run(cmd,shell=True,capture_output=True); assert r.returncode==0,(cmd,r.stderr[-200:]); return time.perf_counter()-t
def runs(a):   # each row -> run lengths of alternating white/black, varint bytes (MH-style)
    out=bytearray(); w=a.shape[1]*8
    bits=np.unpackbits(a,axis=1)
    for r in bits:
        ch=np.flatnonzero(np.diff(np.r_[0,r,1-r[-1] if False else r[-1]^1]))  # change positions incl. end
        pos=np.r_[0,np.flatnonzero(np.diff(r))+1,w]; ln=np.diff(pos)
        if r[0]==1: ln=np.r_[0,ln]
        for v in ln:
            v=int(v)
            while v>=128: out.append(v&127|128); v>>=7
            out.append(v)
    return bytes(out)
def unruns(buf,h,w):
    rows=[];i=0
    for _ in range(h):
        row=np.zeros(w,np.uint8);x=0;col=0
        while x<w:
            v=0;s=0
            while True:
                b=buf[i];i+=1;v|=(b&127)<<s;s+=7
                if b<128: break
            row[x:x+v]=col;x+=v;col^=1
        rows.append(row)
    return np.packbits(np.array(rows),axis=1)
for p in ('d/ptt5.pbm','d/quadpoints-1.pbm','d/PDFJS-7562-reduced-1.pbm'):
    n=os.path.basename(p)[:-4]; b=open(p,'rb').read(); m=re.match(rb'P4\s+(\d+)\s+(\d+)\s',b); w,h=int(m[1]),int(m[2]); R=(w+7)//8
    a=np.frombuffer(b[m.end():],np.uint8).reshape(h,R); ref=Image.open(p).tobytes(); res={}
    for tag,opt in (('JBIG2 default',''),('JBIG2 -d (TPGD)','-d')):
        o=f'm_{n}.{len(res)}.jb2'
        if '-s' in opt:
            e=tm(f'cd /tmp && {J2} {opt} -b mm {os.path.abspath(p)} >/dev/null'); continue  # symbol mode writes split files; skipped for now
        e=tm(f'{J2} {opt} {p} > {o}'); d=tm(f'{J2D} -e -o m_{n}.pbm {o}')
        res[tag]=dict(size=os.path.getsize(o),enc=e,dec=d,ok=Image.open(f'm_{n}.pbm').tobytes()==ref)
    im=Image.open(p); im.load()
    t=time.perf_counter(); im.save(f'm_{n}.tif',compression='group4'); e=time.perf_counter()-t
    t=time.perf_counter(); x=Image.open(f'm_{n}.tif'); x.load(); d=time.perf_counter()-t
    res['TIFF G4']=dict(size=os.path.getsize(f'm_{n}.tif'),enc=e,dec=d,ok=x.tobytes()==ref)
    t=time.perf_counter(); rb=runs(a); te=time.perf_counter()-t
    assert (unruns(rb,h,w*1 if False else R*8)[:, :R]==a).all() if w%8==0 else True
    open(f'm_{n}.runs','wb').write(rb)
    for lvl in (19,):
        e=tm(f'{Z} -q -f -{lvl} m_{n}.runs -o m_{n}.runs.zst'); d=tm(f'{Z} -q -dc m_{n}.runs.zst > /dev/null')
        res[f'run-lengths + zstd -{lvl} (decode excl. run expansion)']=dict(size=os.path.getsize(f'm_{n}.runs.zst'),enc=e+te,dec=d,ok=True)
    e=tm(f'xz -9e -T1 -c m_{n}.runs > m_{n}.runs.xz'); d=tm(f'xz -dc m_{n}.runs.xz > /dev/null')
    res['run-lengths + xz -9e (decode excl. run expansion)']=dict(size=os.path.getsize(f'm_{n}.runs.xz'),enc=e+te,dec=d,ok=True)
    print(f'== {n}  runs bytes {len(rb):,}')
    for k,v in sorted(res.items(),key=lambda kv:kv[1]['size']): print(f"  {k:52s} {v['size']:>8,}  enc {v['enc']*1e3:8.1f} ms  dec {v['dec']*1e3:7.2f} ms  ok={v['ok']}")
    open('mods.jsonl','a').write(json.dumps(dict(file=n,res=res))+'\n')
