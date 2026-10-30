from cli import *
import json,subprocess,hashlib,time,statistics as st,numpy as np,sys,shutil
sys.path.insert(0,S+'/qx'); from var2 import rec
P=S+'/pdf/pdfjs/test/pdfs/'
def md5(p):
    d=f'{W}/rr'; shutil.rmtree(d,ignore_errors=True); os.makedirs(d)
    subprocess.run(['pdftoppm','-r','36','-l','3','-gray',p,d+'/p'],capture_output=True); h=hashlib.md5()
    for f in sorted(os.listdir(d)): h.update(open(d+'/'+f,'rb').read())
    return h.hexdigest()
for fn in ('highlights.pdf','issue16316.pdf','tracemonkey_with_editable_annotations.pdf'):
    src=P+fn; q=f'{W}/{fn}.q.pdf'; h=f'{W}/{fn}.h.pdf'
    eq=med(f'qpdf --object-streams=generate --recompress-flate --compression-level=9 {src} {q}; test -s {q}',3)
    eh=med(f'cd {S}/pdf && python3 hi.py {src} {h} {W}/hw >/dev/null',3)
    ok_q=md5(q)==md5(src); ok_h=md5(h)==md5(src)
    A=[];B=[]
    for i in range(7):
        A.append(run(f'pdftoppm -r 36 -l 3 -gray {q} {W}/tt')[0]); B.append(run(f'pdftoppm -r 36 -l 3 -gray {h} {W}/tt')[0])
    base=dict(size=os.path.getsize(q),enc_s=eq['s'],enc_cpu=eq['cpu'],enc_mb=eq['mb'],dec_s=st.median(A),ok=ok_q)
    cand=dict(size=os.path.getsize(h),enc_s=eh['s'],enc_cpu=eh['cpu'],enc_mb=eh['mb'],dec_s=st.median(B),ok=ok_h)
    log('PDF row filters',fn,os.path.getsize(src),base,cand,dict(note='decode = render first 3 pages, 36 dpi grey (pdftoppm), 7 interleaved pairs; base = qpdf known optimisation; ok = page pixels identical to original'))
# ---- columnar: ORC mr zlib/zstd (R=2), Parquet sao zstd (R=28)
OB=S+'/pq/ob2'
for codec in ('zlib','zstd'):
    f=SL+'mr'; res=[]
    for R in (1,2):
        r=[subprocess.run([OB,f,str(R),f'{W}/mr.{codec}{R}.orc',codec],capture_output=True,text=True,check=True).stdout.split() for _ in range(3)]
        res.append(dict(size=int(r[0][1]),enc_s=st.median(float(x[3]) for x in r)/1e3,dec_s=st.median(float(x[2]) for x in r)/1e3,ok=True))
    log('Detected column layout',f'mr|orc {codec}',os.path.getsize(f),res[0],res[1],dict(note='Apache ORC C++ in-process; write = full writer time; read = median of 9 full scans (median of 3 runs); exact byte check inside tool'))
DB=S+'/pq/bin/duckdb'
def q(sql): return subprocess.run([DB],input=sql,capture_output=True,text=True,check=True).stdout
f=SL+'sao'; d=np.frombuffer(open(f,'rb').read(),np.uint8); res=[]
for R in (1,28):
    n=len(d)//R*R; csv=f'{W}/sao{R}.csv'; np.savetxt(csv,d[:n].reshape(-1,R),fmt='%d',delimiter=',')
    cols=', '.join(f"'c{i}':'UTINYINT'" for i in range(R)); out=f'{W}/sao{R}.parquet'
    q(f"CREATE TABLE t AS SELECT * FROM read_csv('{csv}',header=false,columns={{{cols}}});\nCOPY t TO '{W}/warm.parquet' (FORMAT parquet, COMPRESSION zstd);")
    def wt():
        o=q(f".timer on\nPRAGMA threads=1;\nCREATE TABLE t AS SELECT * FROM read_csv('{csv}',header=false,columns={{{cols}}});\nCOPY t TO '{out}' (FORMAT parquet, COMPRESSION zstd);\n")
        return float(re.findall(r'real ([\d.]+)',o)[-1])
    import re
    w=st.median([wt() for _ in range(3)])
    def rt():
        o=q(f".timer on\nPRAGMA threads=1;\nCREATE TEMP TABLE r AS SELECT * FROM '{out}';\n"); return float(re.findall(r'real ([\d.]+)',o)[-1])
    rd=st.median([rt() for _ in range(7)])
    chk=subprocess.run([DB,'-csv','-noheader','-c',f"SELECT count(*) FROM '{out}';"],capture_output=True,text=True).stdout; ok=int(chk.strip())==n//R
    res.append(dict(size=os.path.getsize(out)+len(d)-n,enc_s=w,dec_s=rd,ok=ok))
log('Detected column layout','sao|parquet zstd',len(d),res[0],res[1],dict(note='DuckDB 1.3.2 single-threaded; write = COPY TO parquet time; read = full CREATE TABLE AS SELECT; row count checked'))
