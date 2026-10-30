from cli import *
import json,subprocess,hashlib,time,statistics as st,numpy as np,re,sys
J=S+'/rv/src/libjxl/b/tools/'; MED=S+'/rv/med/'
def pgmpix(p):
    b=open(p,'rb').read(); h=re.match(rb'P5\s+(\d+)\s+(\d+)\s+(\d+)\s',b); return np.frombuffer(b[h.end():],'>u2').reshape(int(h[2]),int(h[1]))
def jx(name,src,opt,tag):
    o=f'{W}/{name}.{tag}.jxl'; d=f'{W}/{name}.{tag}.dec.pgm'
    r=m('cjxl -d 0 -e 7 '+opt,src,f'{J}cjxl -d 0 -e 7 {opt} --quiet {src} {o}',f'{J}djxl {o} {d} --quiet',o,cmp_raw=d)
    r['ok']=bool((pgmpix(d)==pgmpix(src)).all()); return r
# ---- jxl-hi samples + JPEG2000_UNC re-check (book award vs table conflict)
for name,opt in (('MR2_UNCR','-P 5 -I 0'),('RG3_UNCR','-P 15 -g 0 -I 100'),('x-ray','-P 15 -g 1 -I 100'),('JPEG2000_UNC','-P 5 -I 100')):
    src=MED+name+'.pgm'; b=jx(name,src,'','base'); c=jx(name,src,opt,'hi')
    log('jxl-hi' if name!='JPEG2000_UNC' else 'jxl-hi (recheck of disputed award)',f'{name}|{opt}',os.path.getsize(src),b,c,dict(note='PGM source; round trip checked on pixels'))
# ---- JPEG-LS tuned (in-process CharLS)
def jl(t): return json.loads(subprocess.run([S+'/rv/jls',MED+'x-ray.pgm',f'{W}/x{t}.jls',str(t)],capture_output=True,text=True).stdout)
A=[];B=[]
for i in range(5): A.append(jl(0)); B.append(jl(1))
md=lambda L,k: st.median(x[k] for x in L)
b=dict(size=A[0]['size'],enc_s=md(A,'enc_ms')/1e3,dec_s=md(A,'dec_ms')/1e3,ok=all(x['ok'] for x in A))
c=dict(size=B[0]['size'],enc_s=md(B,'enc_ms')/1e3,dec_s=md(B,'dec_ms')/1e3,ok=all(x['ok'] for x in B))
log('JPEG-LS tuned','x-ray|T=12,32,64 RESET=255',os.path.getsize(MED+'x-ray.pgm'),b,c,dict(note='CharLS (latest GitHub) in-process; 5 interleaved runs, each enc median of 3 and dec median of 9'))
