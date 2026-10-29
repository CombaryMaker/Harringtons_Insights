# Baseline comparison for the Harrington Exchange test files (28 Oct 2026). One decode timing per codec (wall clock, includes process start).
import subprocess as sp,os,time,sys,json,itertools
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; B=S+'/bl'; W=B+'/run/w'; D=S+'/biased/'
ZSTD=S+'/z3/zstd/programs/zstd'; BR=S+'/rv/src/brotli/out/brotli'; Z7=B+'/7zip/CPP/7zip/Bundles/Alone2/b/g/7zz'
ZLI=B+'/openzl/zli'; ZPAQ=S+'/zpaqbin'; PAQ=S+'/cmp_src/paq8px/b/paq8px'; CMIX=S+'/cmp_src/cmix/cmix'; BL=B+'/run/bl'
F={'12bit_noise':'synthetic_12bit_noise.bin','biased_bits':'biased_bits_packed.bin','biased_bytes':'nist/bin/biased-random-bytes.bin',
   '5bit_skewed':'synthetic_5bit_skewed.bin','xray_packed12':'xray_packed12.bin','eeg_1MB':'eeg_1MB.bin'}
def run(c): sp.run(c,shell=True,check=True,stdout=sp.DEVNULL,stderr=sp.DEVNULL)
def td(c): t=time.perf_counter(); run(c); return round((time.perf_counter()-t)*1000,1)
def same(a,b): return open(a,'rb').read()==open(b,'rb').read()
which=sys.argv[1:] or ['zstd','brotli','7z','blosc','openzl','zpaq','paq8px']
res={}
for k,fn in F.items():
  src=D+fn; r={}; o=W+'/'+k
  for m in which:
    try:
      if m=='zstd': run(f'{ZSTD} -q -f --ultra -22 --long=27 {src} -o {o}.zst'); t=td(f'{ZSTD} -q -f -d --long=27 {o}.zst -o {o}.d'); r[m]=(os.path.getsize(o+'.zst'),same(src,o+'.d'),t)
      if m=='brotli': run(f'{BR} -f -q 11 -w 24 {src} -o {o}.br'); t=td(f'{BR} -f -d {o}.br -o {o}.d'); r[m]=(os.path.getsize(o+'.br'),same(src,o+'.d'),t)
      if m=='7z':
        run(f'rm -f {o}.7z; {Z7} a -mx9 -m0=lzma2 -md=64m -mfb=273 -ms=on {o}.7z {src}'); t=td(f'{Z7} e -so {o}.7z > {o}.d'); r[m]=(os.path.getsize(o+'.7z'),same(src,o+'.d'),t)
      if m=='blosc':
        best=None
        for ts,fl in itertools.product([1,2,3,4],[0,1,2]):
          sz,ok,t=sp.run([BL,src,str(ts),str(fl)],capture_output=True,text=True).stdout.split(); sz=int(sz)
          if sz>0 and ok=='1' and (best is None or sz<best[0]): best=(sz,True,float(t),f'typesize {ts}, '+['shuffle','bitshuffle','shuffle+bytedelta'][fl])
        r[m]=best
      if m=='openzl':
        best=None
        for p in ['serial','u8','le-u16','be-u16','le-u32','be-u32','lz']:
          try:
            run(f'{ZLI} compress {src} -o {o}.zl -p {p} -f'); t=td(f'{ZLI} decompress {o}.zl -o {o}.d -f'); sz=os.path.getsize(o+'.zl')
            if same(src,o+'.d') and (best is None or sz<best[0]): best=(sz,True,t,p)
          except Exception as e: pass
        r[m]=best
      if m=='zpaq': run(f'cd {W} && rm -f {k}.zpaq && cp {src} {k} && {ZPAQ} a {k}.zpaq {k} -m5 && rm {k}'); t=td(f'cd {W} && {ZPAQ} x {k}.zpaq -force'); r[m]=(os.path.getsize(o+'.zpaq'),same(src,o),t)
      if m=='paq8px': run(f'cd {W} && cp {src} {k}.p && rm -f {k}.p.paq8px217 && {PAQ} -8 {k}.p'); os.remove(o+'.p'); t=td(f'cd {W} && {PAQ} -d {k}.p.paq8px217 {k}.p'); r[m]=(os.path.getsize(o+'.p.paq8px217'),same(src,o+'.p'),t)
      if m=='cmix': run(f'{CMIX} -c {src} {o}.cmix'); t=td(f'{CMIX} -d {o}.cmix {o}.dc'); r[m]=(os.path.getsize(o+'.cmix'),same(src,o+'.dc'),t)
    except Exception as e: r[m]=('fail',str(e)[:80])
    print(k,m,r[m],flush=True)
  res[k]=r
json.dump(res,open(B+'/run/base_'+'_'.join(which)+'.json','w'),indent=1)
