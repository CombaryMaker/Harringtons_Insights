from cli import *
import statistics as st,json
cases={'lzma-hi sao':(f'xz -dc --format=lzma {W}/sao.base.lzma',f'xz -dc --format=lzma {W}/sao.hi.lzma'),
 'brotli-hi asyoulik.txt|5':(f'{BR} -dc {W}/asyoulik.txt.b.br',f'{BR} -dc {W}/asyoulik.txt.h.br'),
 'brotli-hi xargs.1|11':(f'{BR} -dc {W}/xargs.1.b.br',f'{BR} -dc {W}/xargs.1.h.br')}
notes={}
for k,(a,b) in cases.items():
    A=[];B=[];n=15 if 'sao' in k else 31
    for i in range(n): A.append(run(a+' > /dev/null')[0]); B.append(run(b+' > /dev/null')[0])
    s=f'{st.median(A)*1e3:.2f} -> {st.median(B)*1e3:.2f} ms, cand faster {sum(y<x for x,y in zip(A,B))}/{n}'
    notes[k]=s; print(k,s,flush=True)
open(OUT,'a').write(json.dumps(dict(family='retime (interleaved, CLI)',notes=notes))+'\n')
