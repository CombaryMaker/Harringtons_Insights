import json,os,sys,subprocess,zlib,glob,shutil,collections
sys.path.insert(0,'../qx'); from var2 import rec
def detect(b):
    if len(b)<4096: return 1
    return rec(b[:200000])
def png_up(b,R):
    rows=[b[i:i+R] for i in range(0,len(b),R)]
    out=bytearray(); prev=bytes(R)
    for r in rows:
        r2=r+bytes(R-len(r)) if len(r)<R else r
        out.append(2); out+=bytes((x-y)&255 for x,y in zip(r2,prev)); prev=r2
    return bytes(out)
def best(b):
    cands=[]
    for st in (zlib.Z_DEFAULT_STRATEGY,zlib.Z_FILTERED):
        c=zlib.compressobj(9,zlib.DEFLATED,15,9,st); cands.append((len(z:=c.compress(b)+c.flush()),z,None))
    R=detect(b)
    if 2<=R<=512 and len(b)%R==0:
        u=png_up(b,R)
        for st in (zlib.Z_DEFAULT_STRATEGY,zlib.Z_FILTERED):
            c=zlib.compressobj(9,zlib.DEFLATED,15,9,st); z=c.compress(u)+c.flush()
            cands.append((len(z),z,R))
    return min(cands,key=lambda x:x[0])
def run(pdf,out,work):
    shutil.rmtree(work,ignore_errors=True); os.makedirs(work)
    js=f'{work}/o.json'
    r=subprocess.run(['qpdf','--json-output=2','--decode-level=generalized','--json-stream-data=file',f'--json-stream-prefix={work}/s',pdf,js],capture_output=True)
    if r.returncode not in (0,3): return None
    d=json.load(open(js),parse_float=lambda x:'\x00NUM'+x); objs=d['qpdf'][1]; npred=0
    for k,v in objs.items():
        s=v.get('stream') if isinstance(v,dict) else None
        if not s or 'datafile' not in s: continue
        dct=s['dict']
        if '/Filter' in dct: continue   # still encoded (DCT, JBIG2...) - leave
        p=s['datafile']; b=open(p,'rb').read()
        if len(b)<64: continue
        n,z,R=best(b); open(p,'wb').write(z)
        dct['/Filter']='/FlateDecode'
        if R: dct['/DecodeParms']={'/Predictor':12,'/Columns':R}; npred+=1
    import re; open(js,'w').write(re.sub(r'"\\u0000NUM([^"]*)"',r'\1',json.dumps(d)))
    r=subprocess.run(['qpdf','--json-input',js,'--object-streams=generate','--decode-level=none','--compress-streams=y','--compression-level=9',out],capture_output=True)
    return npred if r.returncode in (0,3) and os.path.exists(out) else None
if __name__=='__main__':
    print(run(sys.argv[1],sys.argv[2],sys.argv[3]))
