import json,subprocess,statistics,collections,os,sys
P=json.load(open('lpick.json')); D=os.path.expanduser('~/dl/cant/canterbury/')
T=collections.defaultdict(list); S={}
for rnd in range(3):
    for f,v in P.items():
        cfgs=list(dict.fromkeys(['lvl=9']+[c for c in (v['A'],v['B']) if c]))
        out=subprocess.run(['./lz','lzma','21',D+f],input='\n'.join(cfgs)+'\n',capture_output=True,text=True).stdout
        for kind,xf in [('xz9e','xzhi/%s.9e.xz'%f),('xzhi','xzhi/%s.xz'%f)]:
            out+=subprocess.run(['./lz','xzdec','21',D+f,xf,kind],capture_output=True,text=True).stdout
        for l in out.splitlines():
            p=l.split()
            if p[0]=='RES':
                assert p[6]=='OK',l
                T[(f,p[1])].append(float(p[5])); S[(f,p[1])]=(int(p[3]),int(p[4]))
rows=[(f,c,S[(f,c)][0],S[(f,c)][1],statistics.median(T[(f,c)])) for (f,c) in S]
json.dump(rows,open('lrt.json','w'))
