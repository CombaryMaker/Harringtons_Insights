# Decode times on the SDR captures (in-process C, median of 15): Exchange (zstd), Exchange->xz, plain zstd -19, xz -9e, xz-hi.
import sys,glob,os,struct,lzma,itertools,subprocess as sp,json
S_='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0,S_+'/biased'); sys.path.insert(0,S_+'/repo/harrington-insights/xz-hi')
from he3 import encode,decode,zc,zd; from xz_hi import best_xz,comp
W=S_+'/sdrt/'; DZ=S_+'/biased/he3dec2'; DX=W+'he3decxz'
best={'1':(16,12,'full'),'2':(8,4,'split'),'3':(16,8,'full'),'4':(8,4,'full'),'5':(8,4,'split'),'6':(8,4,'split')}
def qxz(b): return min((comp(b,lc,lp,pb) for lc,lp,pb in itertools.product(range(5),range(3),range(3)) if lc+lp<=4),key=len)
def run(dec,c,o,p):
    open(W+'c','wb').write(c); open(W+'o','wb').write(o); open(W+'p','wb').write(p)
    return json.loads(sp.run([dec,W+'c',W+'o',W+'p','15'],capture_output=True,text=True).stdout)
for f in sorted(glob.glob(S_+'/sdr/set/*.bin')):
    k=os.path.basename(f)[0]; d=open(f,'rb').read(); J,S,m=best[k]
    e=encode(d,J,S,mode=m,lay=0,level=19); assert decode(e)==d
    rz=run(DZ,e,d,zc(d,19))
    x=encode(d,16,8,mode='split',lay=1,level=19); hdr=x[:25]; J2,S2,mi,lay,K,nb,l1,l2=struct.unpack('<BBBBHQII',x[3:25])
    pre=x[25:len(x)-l1-l2]; s1=zd(x[len(x)-l1-l2:len(x)-l2]); s2=zd(x[len(x)-l2:])
    c1=qxz(s1); c2=qxz(s2); xc=x[:3]+struct.pack('<BBBBHQII',J2,S2,mi,lay,K,nb,len(c1),len(c2))+pre+c1+c2
    base,hi=best_xz(d); r1=run(DX,xc,d,hi); r2=run(DX,xc,d,base)
    print(os.path.basename(f),json.dumps({'exchange_zstd':(len(e),rz['exchange_decode_ms'],rz['ok']),'zstd19_ms':rz['plain_zstd_decode_ms'],
      'exchange_xz':(len(xc),r1['exchange_decode_ms'],r1['ok']),'xz_hi':(len(hi),r1['plain_xz_decode_ms']),'xz9e':(len(base),r2['plain_xz_decode_ms'])}),flush=True)
