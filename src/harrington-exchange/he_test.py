# Harrington Exchange (from Michael's HEJug code), fixed to be lossless:
# 1) Exchange: rank the 8-bit chunks by frequency and map them, most common first,
#    onto target patterns sorted by "most 0s in the first jLen bits, then most 0s overall".
#    All 256 values are mapped (a full swap table), so nothing can collide.
# 2) Juggle: split every mapped chunk into its first jLen bits (stream 1) and the
#    rest (stream 2, reversed), as in the original code.
import numpy as np, subprocess, os, sys, zlib
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; Z=S+'/z3'; C=S+'/cant_dl/canterbury-corpus/canterbury'
FILES=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
def targets(j):
    # most 0s in the first j bits, then most 0s overall
    def zeros_first(v): return j - bin(v >> (8 - j)).count('1')
    def zeros_all(v): return 8 - bin(v).count('1')
    return sorted(range(256), key=lambda v: (-zeros_first(v), -zeros_all(v), v))
def he(data, j):
    a=np.frombuffer(data,dtype=np.uint8)
    cnt=np.bincount(a,minlength=256)
    src=sorted(range(256),key=lambda v:(-cnt[v],v))
    tg=targets(j); m=np.zeros(256,dtype=np.uint8)
    for s,t in zip(src,tg): m[s]=t
    b=m[a]
    hi=(b>>(8-j)).astype(np.uint8); lo=(b&((1<<(8-j))-1)).astype(np.uint8)
    return m, hi, lo[::-1].copy()
def packbits(sym, w):   # pack w-bit symbols tightly
    bits=np.unpackbits(sym[:,None],axis=1)[:,8-w:].ravel()
    return np.packbits(bits).tobytes()
def comp(b, tool):
    cmd={'zstd':[Z+'/zstd/programs/zstd','-q','--ultra','-22','-c'],'lz4':[Z+'/lz4/lz4','-q','-12','-c'],'gzip':['gzip','-9n','-c']}[tool]
    return len(subprocess.run(cmd,input=b,capture_output=True).stdout)
tot={}
for fn in FILES:
    d=open(C+'/'+fn,'rb').read(); row=[fn]
    for tool in ('zstd','lz4','gzip'):
        base=comp(d,tool); res=[base]
        for j in (1,2,3,4):
            m,hi,lo=he(d,j)
            tight=comp(packbits(hi,j),tool)+comp(packbits(lo,8-j),tool)+256
            bytewise=comp(hi.tobytes(),tool)+comp(lo.tobytes(),tool)+256
            mapped_only=comp(bytes(m[np.frombuffer(d,dtype=np.uint8)]),tool)+256
            res+= [tight,bytewise,mapped_only]
        for k,v in enumerate(res): tot.setdefault((tool,k),0); tot[(tool,k)]+=v
        best=min(res[1:]); row.append(f'{tool} {base} best-HE {best}')
    print(' | '.join(row), flush=True)
names=['plain']+[f'j{j} {k}' for j in (1,2,3,4) for k in ('tight','bytewise','remap-only')]
for tool in ('zstd','lz4','gzip'):
    print(tool, ' '.join(f'{names[k]}={tot[(tool,k)]}' for k in range(len(names))))
