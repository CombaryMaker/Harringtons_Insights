import subprocess,os,sys,re,statistics as st
import numpy as np
sys.path.insert(0,'../qx'); from var2 import rec
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
DB=S+'/pq/bin/duckdb'; W=S+'/pq/w'; os.makedirs(W,exist_ok=True)
FILES=[('Canterbury',S+'/cant_dl/canterbury-corpus/canterbury/'+f) for f in ('kennedy.xls','ptt5','sum')]+[('Silesia',S+'/sil/f/'+f) for f in ('mr','x-ray','sao','osdb')]
def q(sql): return subprocess.run([DB,'-csv','-noheader','-c',sql],capture_output=True,text=True,check=True).stdout
def build(path,R,codec,tag):
    d=np.frombuffer(open(path,'rb').read(),np.uint8); n=len(d)//R*R
    m=d[:n].reshape(-1,R); csv=f'{W}/t.csv'
    np.savetxt(csv,m,fmt='%d',delimiter=',')
    cols=', '.join(f"'c{i}':'UTINYINT'" for i in range(R))
    out=f'{W}/{os.path.basename(path)}.{tag}.parquet'
    q(f"PRAGMA threads=1; COPY (SELECT * FROM read_csv('{csv}',header=false,columns={{{cols}}})) TO '{out}' (FORMAT parquet, COMPRESSION {codec});")
    tail=len(d)-n
    chk=q(f"SELECT count(*), {'+'.join(f'sum(c{i})' for i in range(R))} FROM '{out}';")
    assert int(chk.strip().split(',')[0])==len(m)
    return out,os.path.getsize(out)+tail
def rt(f):
    o=subprocess.run([DB],input=f".timer on\nPRAGMA threads=1;\nCREATE TEMP TABLE t AS SELECT * FROM '{f}';\n",capture_output=True,text=True,check=True).stdout
    return float(re.findall(r'real ([\d.]+)',o)[-1])
def rtime(f): return st.median([rt(f) for _ in range(7)])
for codec in ('snappy','zstd'):
    print('== codec',codec,flush=True)
    for corp,p in FILES:
        U=os.path.getsize(p); R=max(rec(open(p,'rb').read()),2)
        b,bs=build(p,1,codec,f'{codec}1'); u,us=build(p,R,codec,f'{codec}{R}')
        tb=rtime(b); tu=rtime(u); m=lambda x:round(x/U*1e6)
        print(f'{corp} {os.path.basename(p)} R={R}: 1 column {m(bs)} ({tb*1e3:.1f} ms) -> {R} columns {m(us)} ({tu*1e3:.1f} ms)',flush=True)
