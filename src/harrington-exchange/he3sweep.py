import sys,json; sys.path.insert(0,'.'); from he3 import *
name,f=sys.argv[1],sys.argv[2]; d=open(f,'rb').read()
rows=[]
for S in range(1,24):
    for J in range(S+1,25):
        b=min(len(encode(d,J,S,mode='full',lay=lay,level=19)) for lay in (0,1))
        rows.append((b,S,J))
rows.sort(); json.dump(rows,open(f'he3sweep_{name}.json','w'))
print(name,'best 5 at zstd -19 (size, swap, juggle):',rows[:5],flush=True)
