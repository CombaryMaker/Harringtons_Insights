from cli import *
import json,subprocess
mod={json.loads(l)['file']:json.loads(l) for l in open(S+'/modern/results.jsonl')}
def sz(cmd,o): subprocess.run(cmd,shell=True,check=True); return os.path.getsize(o)
# ---- lzma-hi (liblzma .lzma format; book used 7-Zip SDK)
for fn in ('sao','x-ray','xml'):
    f=SL+fn; o=f'{W}/{fn}.s.lzma'; best=None
    for lc,lp,pb in ((3,0,2),(3,0,0),(0,0,0),(4,0,0),(2,0,0),(1,2,2),(0,2,2),(0,1,1),(2,1,1),(4,0,2)):
        for nice in (273,64):
            a=f'--format=lzma --lzma1=preset=9e,lc={lc},lp={lp},pb={pb},nice={nice}'
            s=sz(f'xz -T1 {a} -c {f} > {o}',o)
            if best is None or s<best[0]: best=(s,a)
    ob=f'{W}/{fn}.base.lzma'; b=m('xz --format=lzma -9e',f,f'xz -T1 --format=lzma -9e -c {f} > {ob}',f'xz -dc --format=lzma {ob}',ob)
    oc=f'{W}/{fn}.hi.lzma'; c=m('lzma-hi '+best[1],f,f'xz -T1 {best[1]} -c {f} > {oc}',f'xz -dc --format=lzma {oc}',oc)
    log('lzma-hi',fn,os.path.getsize(f),b,c,dict(note='liblzma .lzma used in place of the 7-Zip SDK; search grid 10 lc/lp/pb x 2 nice'))
# ---- zstd-hi
grid=['windowLog=27','searchLog=9','minMatch=3','minMatch=4','minMatch=5','minMatch=6','targetLength=999','strategy=9','searchLog=7,minMatch=5','strategy=8','strategy=7','minMatch=7','searchLog=8']
for fn,L,src in (('counties2023.jsonl',22,MD),('plrabn12.txt',9,CT),('ptt5',3,CT)):
    f=src+fn; o=f'{W}/{fn}.z.zst'; flag=('--ultra ' if L>19 else '')+f'-{L}'; best=None
    if fn in mod: best=(0,mod[fn]['res']['zstd-hi (vs zstd -22)']['cfg'])
    else:
        for g in grid:
            try: s=sz(f'{Z} -q -f {flag} --zstd={g} -c {f} > {o}',o)
            except Exception: continue
            if best is None or s<best[0]: best=(s,f'{flag} --zstd={g}')
    ob=f'{W}/{fn}.b.zst'; b=m('zstd '+flag,f,f'{Z} -q -f {flag} -c {f} > {ob}',f'{Z} -q -dc {ob}',ob)
    oc=f'{W}/{fn}.h.zst'; c=m('zstd-hi '+best[1],f,f'{Z} -q -f {best[1]} -c {f} > {oc}',f'{Z} -q -dc {oc}',oc)
    log('zstd-hi',f'{fn}|{L}',os.path.getsize(f),b,c)
# ---- brotli-hi (encoder params via library; stock brotli -d decodes)
for fn,q in (('asyoulik.txt',5),('sum',8),('xargs.1',11)):
    f=CT+fn; o=f'{W}/{fn}.t.br'; best=None
    for mode in (0,1,2):
        for lw in (16,18,20,22,24):
            for lb in (0,16,18,20,24):
                for dl in (0,1):
                    s=sz(f'{BE} {f} {o} {q} {mode} {lw} {lb} {dl}',o)
                    if best is None or s<best[0]: best=(s,(mode,lw,lb,dl))
    ob=f'{W}/{fn}.b.br'; b=m(f'brotli -q {q}',f,f'{BR} -f -q {q} -o {ob} {f}',f'{BR} -dc {ob}',ob)
    mo,lw,lb,dl=best[1]; oc=f'{W}/{fn}.h.br'
    c=m(f'brotli-hi mode={mo} lgwin={lw} lgblock={lb} dlcm={dl}',f,f'{BE} {f} {oc} {q} {mo} {lw} {lb} {dl}',f'{BR} -dc {oc}',oc)
    log('brotli-hi',f'{fn}|{q}',os.path.getsize(f),b,c)
