import json,glob,subprocess,time,statistics,os,sys
Z=os.path.expanduser('~/dl/7zip/CPP/7zip/Bundles/Alone2/b/g/7zz')
def dec(arc,orig_size):
    s=time.perf_counter(); p=subprocess.run([Z,'e','-so','-mmt1',arc],capture_output=True); t=time.perf_counter()-s
    assert p.returncode==0 and len(p.stdout)==orig_size
    return t*1000,p.stdout
out={}
for d,reps in [('cant',15),('sil',7)]:
    for jf in sorted(glob.glob(d+'/*.json')):
        r=json.load(open(jf)); f=r['file']; src={'cant':os.path.expanduser('~/dl/cant/canterbury/'),'sil':os.path.expanduser('~/sil/SilesiaCorpus/')}[d]+f
        orig=open(src,'rb').read()
        arcs={'base':f'{d}/{f}.base.7z'}; arcs.update({k:f'{d}/{f}.{k}.7z' for k in r['cands']})
        T={k:[] for k in arcs}
        for k,a in arcs.items():
            _,o=dec(a,len(orig)); assert o==orig, (f,k)
        for i in range(reps):
            for k,a in arcs.items(): T[k].append(dec(a,len(orig))[0])
        out[f'{d}/{f}']={k:(os.path.getsize(a),statistics.median(T[k]),(r['cands'][k][1] if k!='base' else '-mx9')) for k,a in arcs.items()}
        out[f'{d}/{f}']['orig']=len(orig)
        print(f,flush=True)
json.dump(out,open('dt.json','w'))
