import subprocess, re, json, time, os
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
C=S+'/cant_dl/canterbury-corpus/canterbury'; P=S+'/cmp_src/cmixlab/pre'
TEXT=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','lcet10.txt','plrabn12.txt','xargs.1']
BIN=['kennedy.xls','ptt5','sum']; ALL=TEXT[:5]+['kennedy.xls']+TEXT[5:7]+['ptt5','sum','xargs.1']
def run(b, paths, reps=3):
    best=None
    for r in range(reps):
        out=subprocess.run([b]+paths,capture_output=True,text=True,cwd=S+'/cpp_port/trit').stdout
        sz={}; d=0.0
        for l in out.splitlines():
            m=re.match(r'(\S+)\s+(\d+) -> +(\d+)\s+c ([\d.]+)s d ([\d.]+)s',l)
            if m: sz[m.group(1).replace('.pre','')]=int(m.group(3)); d+=float(m.group(5))
        if best is None or d<best[1]: best=(sz,d)
    return best
def undo_time():
    t=time.time()
    for f in TEXT: subprocess.run([S+'/cmp_src/cmixlab/unpre',S+'/cmp_src/cmix/dictionary/english.dic',P+'/'+f+'.pre',S+'/un.tmp'],capture_output=True)
    return time.time()-t
V={}
for name,b in [('HCM v3 fast (original)','./pb0'),('HCM v3.3 fast','bin/v33'),('HCM v3.3 max','bin/v33max'),('hcm-trit v2 fast','bin/t2c5'),('hcm-trit v2','bin/t2c6'),('hcm-trit v2 max','bin/t2c6max')]:
    V[name]=run(b,[C+'/'+f for f in ALL])
ut=min(undo_time() for _ in range(3))
for name,b in [('HCM v3.3 fast + dictionary','bin/v33'),('hcm-trit v2 max + dictionary','bin/t2c6max')]:
    t=run(b,[P+'/'+f+'.pre' for f in TEXT]); bn=run(b,[C+'/'+f for f in BIN])
    V[name]=({**t[0],**bn[0]}, t[1]+bn[1]+ut)
R={}
for l in open(S+'/cpp_port/bench_final2.out'):
    l=l.strip()
    if l.startswith('{'):
        d=json.loads(l)
        if d['tool'] in ('ZPAQ -m5','xz -9e','bzip2 -9'): R.setdefault(d['tool'],{})[d['file']]=(d['out'],d['dc'])
size={f:os.path.getsize(C+'/'+f) for f in ALL}; tin=sum(size.values())
rival={f:min(R[t][f][0] for t in R) for f in ALL}
print('undo dictionary all 8 text files', round(ut,3))
for name,(sz,d) in V.items():
    pm=round(sum(sz[f] for f in ALL)*1e6/tin); wins=sum(sz[f]<rival[f] for f in ALL)
    print(f"{name:32s} {pm:7d}  decode {d:.2f}s  energy {d*15:.1f} J  wins {wins}/11  " + ' '.join(f"{f}:{sz[f]}" for f in ALL))
for t in R:
    pm=round(sum(R[t][f][0] for f in ALL)*1e6/tin); d=sum(R[t][f][1] for f in ALL)
    print(f"{t:32s} {pm:7d}  decode {d:.2f}s  energy {d*15:.1f} J")
