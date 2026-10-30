import os,re,time,json,subprocess,io,glob
from PIL import Image
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
J2=S+'/rv/src/jbig2enc/src/jbig2'; J2D=S+'/rv/src/jbig2dec/jbig2dec'; HX=S+'/modern/bin/hcmx'; RVB=S+'/rv/rvb'
def tm(cmd): t=time.perf_counter(); subprocess.run(cmd,shell=True,check=True,capture_output=True); return time.perf_counter()-t
out=open('fax.jsonl','w')
for p in sorted(glob.glob('d/*.pbm')):
    b=open(p,'rb').read(); m=re.match(rb'P4\s+(\d+)\s+(\d+)\s',b); w,h=int(m[1]),int(m[2]); raw=b[m.end():]; R=(w+7)//8
    n=os.path.basename(p)[:-4]; rf=f'w_{n}.raw'; open(rf,'wb').write(raw); res={}
    im=Image.open(p); im.load()
    for c in ('group3','group4'):
        t=time.perf_counter(); im.save(f'w_{n}.{c}.tif',compression=c); e=time.perf_counter()-t
        t=time.perf_counter(); x=Image.open(f'w_{n}.{c}.tif'); x.load(); d=time.perf_counter()-t
        ok=x.tobytes()==im.tobytes(); res['TIFF '+c]=dict(size=os.path.getsize(f'w_{n}.{c}.tif'),enc_s=e,dec_s=d,ok=ok)
    e=tm(f'{J2} {p} > w_{n}.jb2'); d=tm(f'{J2D} -e -o w_{n}.dec.pbm w_{n}.jb2')
    ok=Image.open(f'w_{n}.dec.pbm').tobytes()==im.tobytes()
    res['JBIG2 generic (jbig2enc)']=dict(size=os.path.getsize(f'w_{n}.jb2'),enc_s=e,dec_s=d,ok=ok)
    for lvl in (1,19):
        r=json.loads(subprocess.run([RVB,'zstd',str(lvl),rf,str(R),'3'],capture_output=True,text=True,env=dict(os.environ,FASTU='1')).stdout)
        res[f'zstd -{lvl}']=dict(size=r['plain'],enc_s=r['enc_plain_us']/1e6,dec_s=r['dec_plain_us']/1e6,ok=True)
        res[f'zstd -{lvl} + row delta']=dict(size=r['delta'],enc_s=r['enc_delta_us']/1e6,dec_s=r['dec_delta_us']/1e6,ok=r['ok'])
        res[f'zstd -{lvl} + activity map']=dict(size=r['act'],enc_s=r['enc_act_us']/1e6,dec_s=r['dec_act_us']/1e6,ok=r['ok'])
    for mo in ('c','x'):
        e=tm(f'{HX} {mo} {rf} w_{n}.{mo}.hcm'); d=tm(f'{HX} d w_{n}.{mo}.hcm w_{n}.{mo}.out')
        res['HCM '+('fast' if mo=='c' else 'max')]=dict(size=os.path.getsize(f'w_{n}.{mo}.hcm'),enc_s=e,dec_s=d,ok=open(f'w_{n}.{mo}.out','rb').read()==raw)
    out.write(json.dumps(dict(file=n,w=w,h=h,raw=len(raw),res=res))+'\n')
    print(f'== {n} {w}x{h} raw {len(raw):,} bytes')
    for k,v in sorted(res.items(),key=lambda kv:kv[1]['size']):
        print(f"  {k:28s} {v['size']:>9,}  enc {v['enc_s']*1e3:9.1f} ms  dec {v['dec_s']*1e3:8.2f} ms  ok={v['ok']}")
