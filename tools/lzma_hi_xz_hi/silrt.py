import json,glob,subprocess,os,statistics,collections
S=os.path.expanduser('~/sil/SilesiaCorpus/')
out={}
for fn in sorted(glob.glob('results/*.json')):
    r=json.load(open(fn)); f=r['file']
    cands=sorted([(v[0],c) for c,v in r['lzma_full'].items() if c!='lvl=9'])
    cfgs=['lvl=9']+[c for _,c in cands]
    T=collections.defaultdict(list); Z={}
    for rnd in range(2):
        o=subprocess.run(['./lz','lzma','5',S+f],input='\n'.join(cfgs)+'\n',capture_output=True,text=True).stdout
        for k,x in [('xz9e','silxz/%s.9e.xz'%f),('xzhi','silxz/%s.hi.xz'%f)]:
            o+=subprocess.run(['./lz','xzdec','5',S+f,x,k],capture_output=True,text=True).stdout
        for l in o.splitlines():
            p=l.split()
            if p[0]=='RES': assert p[6]=='OK',l; T[p[1]].append(float(p[5])); Z[p[1]]=(int(p[3]),int(p[4]))
    out[f]={c:(Z[c][0],Z[c][1],statistics.median(T[c])) for c in Z}
    print(f,flush=True)
json.dump(out,open('silrt.json','w'))
