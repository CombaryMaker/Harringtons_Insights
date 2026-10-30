import subprocess,os,time,numpy as np
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; Z=S+'/z3/zstd/programs/zstd'; U=S+'/modern/bin/undelta'
f=S+'/sil/f/x-ray'; d='xr.d2'
b=np.frombuffer(open(f,'rb').read(),np.uint8); o=b.copy(); o[2:]=(b[2:].astype(np.int16)-b[:-2]).astype(np.uint8); open(d,'wb').write(o.tobytes())
def tm(c): t=time.perf_counter(); subprocess.run(c,shell=True,check=True,capture_output=True); return time.perf_counter()-t
def best(src,L):
    bs=None
    for g in ('windowLog=27','searchLog=9','minMatch=3','minMatch=4','minMatch=5','minMatch=6','minMatch=7','targetLength=999','strategy=9','strategy=8','strategy=7'):
        subprocess.run(f'{Z} -q -f {L} --zstd={g} {src} -o t.zst',shell=True,check=True,capture_output=True); s=os.path.getsize('t.zst')
        if bs is None or s<bs[0]: bs=(s,g)
    return bs
for L in ('-1','-3','-19'):
    e=tm(f'{Z} -q -f {L} {f} -o b.zst'); dd=tm(f'{Z} -q -dc b.zst >/dev/null'); sb=os.path.getsize('b.zst')
    s,g=best(f,L); e2=tm(f'{Z} -q -f {L} --zstd={g} {f} -o h.zst'); d2=tm(f'{Z} -q -dc h.zst >/dev/null')
    s3,g3=best(d,L); e3=tm(f'{Z} -q -f {L} --zstd={g3} {d} -o hd.zst'); d3=tm(f'{Z} -q -dc hd.zst | {U} 2 >/dev/null')
    print(f'zstd {L}: stock {sb:,} dec {dd*1e3:.1f} ms | zstd-hi ({g}) {s:,} dec {d2*1e3:.1f} ms | delta2+zstd-hi ({g3}) {s3+2:,} dec {d3*1e3:.1f} ms (CLI pipe, old inverse)',flush=True)
