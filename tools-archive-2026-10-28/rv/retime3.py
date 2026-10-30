from cli import *
import statistics as st,json
DW=S+'/av/libwebp/b/dwebp'
cases={'FLAC theo':(f'{FL} -d -s -f -o {W}/x.wav {W}/theo.base.flac',f'{FL} -d -s -f -o {W}/x.wav {W}/theo.hi.flac'),
 'WebP 01':(f'{DW} -quiet -ppm {W}/k01.base.webp -o {W}/x.ppm',f'{DW} -quiet -ppm {W}/k01.hi.webp -o {W}/x.ppm'),
 'WebP 10':(f'{DW} -quiet -ppm {W}/k10.base.webp -o {W}/x.ppm',f'{DW} -quiet -ppm {W}/k10.hi.webp -o {W}/x.ppm')}
notes={}
for k,(a,b) in cases.items():
    A=[];B=[]
    for i in range(21): A.append(run(a)[0]); B.append(run(b)[0])
    s=f'{st.median(A)*1e3:.2f} -> {st.median(B)*1e3:.2f} ms, cand faster {sum(y<x for x,y in zip(A,B))}/21'; notes[k]=s; print(k,s,flush=True)
open(OUT,'a').write(json.dumps(dict(family='retime (interleaved, CLI)',notes=notes))+'\n')
