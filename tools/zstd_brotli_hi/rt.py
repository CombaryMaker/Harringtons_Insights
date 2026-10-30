import json,subprocess,sys,collections,statistics,os
comp,fn,key=sys.argv[1],sys.argv[2],sys.argv[3]
P=json.load(open(fn+'.pick.json')); D=os.path.expanduser('~/dl/cant/canterbury/')
rows=[]
for lv,d in P.items():
    base=lv if key=='L' else lv+',w=24'
    for f in d['A']:
        cfgs=list(dict.fromkeys([base]+[c for c in (d['A'][f],d['B'][f]) if c!='base']))
        T=collections.defaultdict(list); S={}
        for rnd in range(3):
            out=subprocess.run(['./hi',comp,'31',D+f],input='\n'.join(cfgs)+'\n',capture_output=True,text=True).stdout
            for l in out.splitlines():
                p=l.split()
                if p[0]=='RES': T[p[1]].append(float(p[5])); S[p[1]]=(int(p[3]),int(p[4]),p[6])
        for c in cfgs: rows.append((lv,f,c,S[c][0],S[c][1],statistics.median(T[c]),S[c][2],c==base, c==d['A'][f], c==d['B'][f]))
json.dump(rows,open(fn+'.rt.json','w'))
