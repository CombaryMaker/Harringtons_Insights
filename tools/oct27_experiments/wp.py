import subprocess,os,glob,time,statistics as st
from PIL import Image
CW='./libwebp/b/cwebp'; DW='./libwebp/b/dwebp'; os.makedirs('wo',exist_ok=True)
def enc(p,opts,tag):
    o=f'wo/{os.path.basename(p)}.{tag}.webp'; subprocess.run([CW,'-quiet','-lossless']+opts+[p,'-o',o],check=True); return o
def dt(f,n=9):
    t=[]
    for _ in range(n):
        s=time.perf_counter(); subprocess.run([DW,'-quiet','-ppm',f,'-o','/dev/null']); t.append(time.perf_counter()-s)
    return st.median(t)
def same(f,p):
    subprocess.run([DW,'-quiet','-ppm',f,'-o','wo/t.ppm'],check=True)
    return Image.open('wo/t.ppm').convert('RGB').tobytes()==Image.open(p).convert('RGB').tobytes()
V={f'm{m}q{q}':['-m',str(m),'-q',str(q)] for m in (4,5,6) for q in (25,50,75,90,100)}
T=[0,0,0,0,0]
for p in sorted(glob.glob('data/kodak/PhotoCD_PCD0992/*.png')):
    im=Image.open(p); raw=im.width*im.height*3
    base=enc(p,['-z','9'],'base'); bs=os.path.getsize(base); assert same(base,p)
    res=sorted((os.path.getsize(enc(p,o,k)),k) for k,o in V.items())
    tb=st.median([dt(base) for _ in range(3)]); win=('base',bs,tb)
    for s,k in res[:4]:
        if s>=bs: break
        f=f'wo/{os.path.basename(p)}.{k}.webp'; assert same(f,p)
        tk=st.median([dt(f) for _ in range(3)])
        if tk<=tb: win=(k,s,tk); break
    m=lambda x:round(x/raw*1e6)
    print(f'{os.path.basename(p)}: -z 9 {m(bs)} ({tb*1e3:.1f} ms) -> {win[0]} {m(win[1])} ({win[2]*1e3:.1f} ms)',flush=True)
    T[0]+=raw;T[1]+=bs;T[2]+=win[1];T[3]+=tb;T[4]+=win[2]
print(f'TOTAL webp lossless -z 9 {round(T[1]/T[0]*1e6)} -> {round(T[2]/T[0]*1e6)} per million; decode {T[3]*1e3:.0f} -> {T[4]*1e3:.0f} ms')
