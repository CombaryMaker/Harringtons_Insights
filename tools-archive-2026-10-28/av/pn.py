import zlib,struct,glob,os,io,time,statistics as st
import numpy as np
from PIL import Image
def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
def filt(a,ft):  # a: HxWx3 uint8 -> filtered rows bytes
    H,W,_=a.shape; bpp=3; rows=a.reshape(H,W*3).astype(np.int16); out=[]
    prev=np.zeros(W*3,np.int16)
    for y in range(H):
        r=rows[y]; left=np.concatenate([np.zeros(bpp,np.int16),r[:-bpp]]); ul=np.concatenate([np.zeros(bpp,np.int16),prev[:-bpp]])
        p=left+prev-ul; pa=abs(p-left); pb=abs(p-prev); pc=abs(p-ul)
        paeth=np.where((pa<=pb)&(pa<=pc),left,np.where(pb<=pc,prev,ul))
        c=[r,r-left,r-prev,r-((left+prev)//2),r-paeth]
        c=[(x&255).astype(np.uint8) for x in c]
        if ft=='A': k=min(range(5),key=lambda i:int(np.abs(c[i].astype(np.int8).astype(np.int32)).sum()))
        else: k=ft
        out.append(bytes([k])+c[k].tobytes()); prev=r
    return b''.join(out)
def png(a,ft,strat,lvl=9,mem=9):
    H,W,_=a.shape; raw=filt(a,ft)
    z=zlib.compressobj(lvl,zlib.DEFLATED,15,mem,strat); d=z.compress(raw)+z.flush()
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+chunk(b'IDAT',d)+chunk(b'IEND',b'')
def dt(b,n=15):
    t=[]
    for _ in range(n):
        s=time.perf_counter(); Image.open(io.BytesIO(b)).load(); t.append(time.perf_counter()-s)
    return st.median(t)
T=[0,0,0,0,0]
for p in sorted(glob.glob('data/kodak/PhotoCD_PCD0992/*.png')):
    im=Image.open(p).convert('RGB'); a=np.array(im); raw=a.size
    bio=io.BytesIO(); im.save(bio,'PNG',optimize=True); base=bio.getvalue()
    cands=[]
    for ft in (0,1,2,3,4,'A'):
        for s in (zlib.Z_DEFAULT_STRATEGY,zlib.Z_FILTERED,zlib.Z_RLE):
            cands.append((png(a,ft,s),f'f{ft}s{s}'))
    cands.sort(key=lambda x:len(x[0]))
    tb=st.median([dt(base) for _ in range(3)]); win=('base',len(base),tb)
    for b,k in cands[:4]:
        if len(b)>=len(base): break
        assert np.array_equal(np.array(Image.open(io.BytesIO(b)).convert('RGB')),a)
        tk=st.median([dt(b) for _ in range(3)])
        if tk<=tb: win=(k,len(b),tk); break
    m=lambda x:round(x/raw*1e6)
    print(f'{os.path.basename(p)}: Pillow optimize {m(len(base))} ({tb*1e3:.1f} ms) -> {win[0]} {m(win[1])} ({win[2]*1e3:.1f} ms)',flush=True)
    T[0]+=raw;T[1]+=len(base);T[2]+=win[1];T[3]+=tb;T[4]+=win[2]
print(f'TOTAL PNG {round(T[1]/T[0]*1e6)} -> {round(T[2]/T[0]*1e6)} per million; decode {T[3]*1e3:.0f} -> {T[4]*1e3:.0f} ms')
