# flac-hi search (27 Oct 2026): per file, flac -8 plus block size and flac -e/-p/-r 8 candidates; keeps the
# smallest candidate that the stock flac decoder decodes exactly and no slower (median of repeated timings).
import subprocess,os,glob,time,statistics as st,itertools
F='./flac/b/src/flac/flac'; os.makedirs('fo',exist_ok=True)
def enc(w,opts,tag):
    o=f'fo/{os.path.basename(w)}.{tag}.flac'
    subprocess.run([F,'-s','-f','-o',o]+opts+[w],check=True); return o
def dt(f,n=9):
    t=[]
    for _ in range(n):
        s=time.perf_counter(); subprocess.run([F,'-d','-s','-c','-f',f],stdout=subprocess.DEVNULL); t.append(time.perf_counter()-s)
    return st.median(t)
def same(f,w):
    a=subprocess.run([F,'-d','-s','-c','-f','--force-raw-format','--endian=little','--sign=signed',f],capture_output=True).stdout
    import wave; x=wave.open(w); return a==x.readframes(x.getnframes())
V={}
for b in (576,1152,2304,4096,4608):
    for extra,n in (([], ''),(['-e'],'e'),(['-e','-p'],'ep'),(['-r','8'],'r8'),(['-e','-r','8'],'er8')):
        V[f'b{b}{n}']=['-8','-b',str(b)]+extra
T=[0,0,0,0,0]
for w in sorted(glob.glob('wav/*.wav')):
    raw=os.path.getsize(w); base=enc(w,['-8'],'base'); bs=os.path.getsize(base)
    res=sorted(((os.path.getsize(enc(w,o,k)),k) for k,o in V.items()))
    tb=st.median([dt(base) for _ in range(3)]); win=('base',bs,tb)
    for s,k in res[:5]:
        if s>=bs: break
        f=f'fo/{os.path.basename(w)}.{k}.flac'; assert same(f,w)
        tk=st.median([dt(f) for _ in range(3)])
        if tk<=tb: win=(k,s,tk); break
    m=lambda x:round(x/raw*1e6)
    print(f'{os.path.basename(w)}: flac -8 {m(bs)} ({tb*1e3:.1f} ms) -> {win[0]} {m(win[1])} ({win[2]*1e3:.1f} ms); smallest any {res[0][1]} {m(res[0][0])}',flush=True)
    T[0]+=raw;T[1]+=bs;T[2]+=win[1];T[3]+=tb;T[4]+=win[2]
print(f'TOTAL flac -8 {round(T[1]/T[0]*1e6)} -> {round(T[2]/T[0]*1e6)} per million; decode {T[3]*1e3:.0f} -> {T[4]*1e3:.0f} ms')
