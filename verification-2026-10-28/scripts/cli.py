import os,sys,json,subprocess,statistics as st,shutil,itertools
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
MR=S+'/modern/bin/memrun'; W=S+'/rv/w'; os.makedirs(W,exist_ok=True)
CT=S+'/cant_dl/canterbury-corpus/canterbury/'; SL=S+'/sil/f/'; MD=S+'/modern/data/'
Z=S+'/z3/zstd/programs/zstd'; Z7=S+'/rv/src/7zip/CPP/7zip/Bundles/Alone2/b/g/7zz'; BR=S+'/rv/src/brotli/out/brotli'; BE=S+'/rv/brenc'
RAR=S+'/rarx/rar/rar'; UNRAR=S+'/rarx/rar/unrar'; HX=S+'/modern/bin/hcmx'; BZH=S+'/winbuild/bzip2-hi-linux'; FL=S+'/av/flac/b/src/flac/flac'
OUT=S+'/rv/results.jsonl'
def run(cmd,env=None):
    e=dict(os.environ,**(env or {}))
    r=subprocess.run([MR,cmd],capture_output=True,env=e)
    l=[x for x in r.stderr.decode(errors='replace').splitlines() if x.startswith('MEMRUN')][-1].split()
    assert int(l[4])==0,(cmd,r.stderr[-300:]); return float(l[1]),float(l[2]),int(l[3])/1024
def med(cmd,n,env=None):
    r=[run(cmd,env) for _ in range(n)]
    return dict(s=st.median(x[0] for x in r),cpu=st.median(x[1] for x in r),mb=max(x[2] for x in r))
def m(name,src,enc,dec,out,env=None,size=None,cmp_raw=None):
    """enc: command producing out; dec: command writing decoded bytes to stdout (or to cmp file if cmp_raw)."""
    if os.path.exists(out): os.remove(out)
    e=med(enc,3,env)
    sz=size(out) if size else os.path.getsize(out)
    if cmp_raw: subprocess.run(dec,shell=True,check=True); ok=open(cmp_raw,'rb').read()==open(src,'rb').read(); d=med(dec,5,env)
    else:
        ok=subprocess.run(dec,shell=True,capture_output=True,env=dict(os.environ,**(env or {}))).stdout==open(src,'rb').read(); d=med(dec+' > /dev/null',5,env)
    return dict(name=name,size=sz,enc_s=e['s'],enc_cpu=e['cpu'],enc_mb=e['mb'],dec_s=d['s'],dec_cpu=d['cpu'],dec_mb=d['mb'],ok=ok)
def log(fam,sample,U,base,cand,extra=None):
    rec=dict(family=fam,sample=sample,orig=U,base=base,cand=cand,**(extra or {}))
    rec['verdict']='No-Pain' if cand['ok'] and base['ok'] and cand['size']<base['size'] and cand['dec_s']<=base['dec_s'] else ('smaller, slower' if cand['size']<base['size'] else 'not smaller')
    open(OUT,'a').write(json.dumps(rec)+'\n'); print(fam,sample,base['size'],'->',cand['size'],f"dec {base['dec_s']*1e3:.1f}->{cand['dec_s']*1e3:.1f} ms",f"enc {base['enc_s']:.2f}->{cand['enc_s']:.2f} s",rec['verdict'],flush=True)
def xzs(f,args,ext='.xz'): o=f'{W}/{os.path.basename(f)}{ext}'; return o,f'xz -T1 {args} -c {f} > {o}'
