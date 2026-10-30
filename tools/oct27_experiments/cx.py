import os,math,heapq,glob,gzip as gz
from collections import Counter
from var import sz,CMP,delta,nibsplit,C
from var2 import rec
FILES=[f for f in sorted(os.listdir(C)) if f!='SHA1SUM']
def packbits(bits):
    bits=bits+[0]*(-len(bits)%8); return bytes(sum(bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(bits),8))
# ---------- Combary A: 2 = cut marker. Segments get their own transform; cut positions cost log2(3) per marked unit
def seg_transform(b,blk):
    outs=[];choices=[]
    for i in range(0,len(b),blk):
        s=b[i:i+blk]; R=rec(s)
        cands={'p':s,'d1':delta(s,1),'dR':delta(s,R)}
        k=min(cands,key=lambda k:sz('zstd',cands[k][:]) if False else len(__import__('zlib').compress(cands[k],6)))
        choices.append((k,R)); outs.append(cands[k])
    # Combary marker stream: one trit-ish symbol per block where choice changes (2) else continue(0/1 unused) -> cost
    changes=sum(1 for a,b2 in zip(choices,choices[1:]) if a!=b2)
    hdr=math.ceil((len(choices)*math.log2(3)+changes*8)/8)+2
    return b''.join(outs),hdr,choices
# ---------- Combary B: fixed-length window, 2s mark active (changed) positions vs record above; stream of flags + literals
def activity(b,R):
    flags=[0 if i>=R and b[i]==b[i-R] else 1 for i in range(len(b))]
    lits=bytes(b[i] for i in range(len(b)) if flags[i])
    return [packbits(flags),lits]
def activity_delta(b,R):
    flags=[0 if i>=R and b[i]==b[i-R] else 1 for i in range(len(b))]
    d=delta(b,R); lits=bytes(d[i] for i in range(len(b)) if flags[i])
    return [packbits(flags),lits]
# ---------- Huffman
def hufflens(freq):
    h=[(f,i,[s]) for i,(s,f) in enumerate(freq.items()) if f>0]; heapq.heapify(h); L={s:0 for s in freq}
    if len(h)==1: L[h[0][2][0]]=1; return L
    n=len(h)
    while len(h)>1:
        a=heapq.heappop(h); c=heapq.heappop(h)
        for s in a[2]+c[2]: L[s]+=1
        heapq.heappush(h,(a[0]+c[0],n,a[2]+c[2])); n+=1
    return L
def hshrink(n,total=256): return sum(math.ceil(math.log2(r)) for r in range(total,total-n,-1) if r>1)
def huff_bits(b,L): return sum(L.get(x,99) for x in b)
def eng_table():
    t=b''
    for f in glob.glob('/usr/share/common-licenses/*'):
        try: t+=open(f,'rb').read()
        except: pass
    for f in glob.glob('/usr/share/doc/*/*.gz')[:400]:
        try: t+=gz.open(f).read()
        except: pass
    c=Counter(t)
    for v in range(256): c[v]+=1      # every byte codable
    return hufflens(c),len(t)
def main():
    print('== Combary A: segment cuts (2 = cut) ==',flush=True)
    for blk in (4096,16384,65536):
        T={c:[0,0] for c in CMP}
        for f in FILES:
            b=open(C+'/'+f,'rb').read(); t,h,_=seg_transform(b,blk)
            for c in CMP:
                p=sz(c,b); q=sz(c,t)+h; T[c][0]+=p; T[c][1]+=min(p+1,q)
        print('blk',blk,T,flush=True)
    print('== Combary B: activity map (2 = active position) ==',flush=True)
    for f in FILES:
        b=open(C+'/'+f,'rb').read(); R=rec(b); line=[f'{f} R={R}']
        for c in CMP:
            p=sz(c,b); dR=sz(c,delta(b,R))
            a=sum(sz(c,s) for s in activity(b,R))+6; ad=sum(sz(c,s) for s in activity_delta(b,R))+6
            line.append(f'{c} plain {p} delta {dR} act {a} actdelta {ad}')
        print(' | '.join(line),flush=True)
    print('== Huffman header tests (order-0 only, bits) ==',flush=True)
    E,n=eng_table(); print('English table trained on',n,'bytes outside corpus',flush=True)
    for f in FILES:
        b=open(C+'/'+f,'rb').read(); c=Counter(b); L=hufflens(c)
        body=huff_bits(b,L); used=len(c)
        raw=256*4                                  # 4 bits per length for 256 symbols (lengths<=15 assumed)
        harr=hshrink(used)+used*4                  # Harrington shrinking list of used symbols + 4-bit length each
        static=huff_bits(b,E)                      # fixed English table, no header
        print(f'{f}: own-table body {body} + raw hdr {raw} = {body+raw} | + Harrington hdr {harr} = {body+harr} | static English (no hdr) {static} | gzip {sz("gzip",b)*8}',flush=True)
main()
