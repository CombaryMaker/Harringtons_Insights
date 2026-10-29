# SDR captures: xz -9e vs xz-hi (No-Pain xz search) vs Harrington Exchange streams compressed with xz-hi (Exchange->xz mutant).
import sys,glob,os,lzma,numpy as np
S_='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0,S_+'/biased'); sys.path.insert(0,S_+'/repo/harrington-insights/xz-hi')
from he3 import to_vals,to_bits,targets,perm,apply,layout_pack; from xz_hi import best_xz, comp
import itertools
def quick(d): return min(len(comp(d,lc,lp,pb)) for lc,lp,pb in itertools.product(range(5),range(3),range(3)) if lc+lp<=4)
def streams(d,J,S,mode):
    v,tail=to_vals(d,J); src=[]
    if mode=='full':
        u,c=np.unique(v,return_counts=True); src=[int(x) for x in u[np.argsort(-c,kind='stable')][:256]]; v=apply(v,perm(src,targets(J,S,len(src))))
    b=to_bits(v,J); hi=b[:,:S]; lo=b[:,S:][::-1]
    return [layout_pack(hi,1),layout_pack(lo,1)], (len(src)*J+7)//8+25+(len(tail)+7)//8
for f in sorted(glob.glob(S_+'/sdr/set/*.bin'))[1:]:
    d=open(f,'rb').read(); base,hi=best_xz(d); r={'xz9e':len(base),'xz_hi':len(hi)}; best=None
    for J,S in ((8,4),(16,8)):
        for mode in ('full','split'):
            ss,h=streams(d,J,S,mode); sz=h+sum(quick(s) for s in ss)
            if best is None or sz<best[0]: best=(sz,J,S,mode)
    r['exchange_xzhi']=best; print(os.path.basename(f),len(d),r,flush=True)
