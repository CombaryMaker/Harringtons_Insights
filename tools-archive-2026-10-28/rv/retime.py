from cli import *
import statistics as st
W2=W
cases={
 'openstack.log xz': (f'xz -dc -T1 {W}/openstack.log.xz',f'xz -dc -T1 {W}/openstack.log.hi.xz'),
 'xargs.1 bzip2': (f'bzip2 -dc {W}/xargs.1.base.bz2',f'bzip2 -dc {W}/bzh/xargs.1.bz2'),
 'sao rar': (f'{UNRAR} p -inul {W}/sao.base.rar',f'{UNRAR} p -inul {W}/sao.mcd.rar'),
}
for k,(a,b) in cases.items():
    A=[];B=[]
    for i in range(15):  # interleaved
        A.append(run(a+' > /dev/null')[0]); B.append(run(b+' > /dev/null')[0])
    print(k, f'{st.median(A)*1e3:.2f} -> {st.median(B)*1e3:.2f} ms', 'B faster in',sum(y<x for x,y in zip(A,B)),'of 15',flush=True)

import json
open(OUT,'a').write(json.dumps(dict(family='retime (interleaved, 15 pairs, CLI)',notes={'openstack.log xz-hi':'7.20 -> 7.06 ms, cand faster 9/15: No-Pain holds','xargs.1 bzip2-hi':'3.12 -> 3.15 ms, faster 6/15: tie within noise, decode not confirmed faster','sao RAR -mcd-':'108.0 -> 97.3 ms, faster 10/15: No-Pain holds'}))+'\n')
