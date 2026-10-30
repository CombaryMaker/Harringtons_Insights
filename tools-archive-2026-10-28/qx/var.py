import subprocess,os,sys
C='../cant_dl/canterbury-corpus/canterbury';Z='../z3'
CMP={'zstd':[Z+'/zstd/programs/zstd','-q','--ultra','-22','-c'],'lz4':[Z+'/lz4/lz4','-q','-12','-c'],'gzip':['gzip','-9nc']}
def sz(c,d): return len(subprocess.run(CMP[c],input=d,capture_output=True).stdout)
def cat(streams,c): return sum(sz(c,s) for s in streams)
def nibsplit(b):  # juggle at nibble: hi stream, lo stream (lo reversed per HE juggle)
    hi=bytes(b[i]>>4<<4|b[i+1]>>4 for i in range(0,len(b)-1,2)); lo=bytes((b[i]&15)<<4|(b[i+1]&15) for i in range(0,len(b)-1,2))
    t=b[-1:] if len(b)%2 else b''; return [hi+t,lo[::-1]]
def planes(b):
    out=[]
    for k in range(8):
        bits=[(x>>k)&1 for x in b]; bits+= [0]*(-len(bits)%8)
        out.append(bytes(sum(bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(bits),8)))
    return [b''.join(out)]
def freqrank(b):  # Harrington-style byte rank: most common -> 0, next -> 1 ...
    from collections import Counter
    c=Counter(b); order=sorted(range(256),key=lambda v:(-c[v],v)); m=[0]*256
    for i,v in enumerate(order): m[v]=i
    return bytes(m[x] for x in b)
def delta(b,s): return bytes((b[i]-(b[i-s] if i>=s else 0))&255 for i in range(len(b)))
V={'nibble-juggle':lambda b:(nibsplit(b),0),
   'rank+nibble-juggle':lambda b:(nibsplit(freqrank(b)),256),
   'bitplanes':lambda b:(planes(b),0),
   'rank+bitplanes':lambda b:(planes(freqrank(b)),256),
   'stride-juggle-2':lambda b:([b[0::2],b[1::2]],0),
   'stride-juggle-4':lambda b:([b[i::4] for i in range(4)],0),
   'stride-juggle-13':lambda b:([b[i::13] for i in range(13)],0)}
if __name__=="__main__":
  pass
def _main():
    tot={c:[0,0] for c in CMP}
    for f in sorted(os.listdir(C)):
        if f=='SHA1SUM': continue
        b=open(C+'/'+f,'rb').read(); line=[f]
        for c in CMP:
            p=sz(c,b); best,bn=p,'plain'
            for n,fn in V.items():
                s,h=fn(b); r=cat(s,c)+h+1+4*(len(s)-1)
                if r<best: best,bn=r,n
            tot[c][0]+=p; tot[c][1]+=min(best,p+1); line.append(f'{c} {p}->{min(best,p+1)} {bn}')
        print(' | '.join(line),flush=True)
    print(tot)
    
if __name__=="__main__": _main()
