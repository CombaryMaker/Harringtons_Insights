import re
B='/mnt/user-data/outputs/book2/'
CU=2.810784; TU=1.229584; SU=211.93858
NUM=re.compile(r'\d{1,3}(?:,\d{3})+')
def b(v,u): return f"{round(int(v.replace(',',''))*u):,}"
def cvcols(line,cols):
    c=line.split('|')
    for j,u in cols.items():
        c[j]=NUM.sub(lambda m: b(m.group(0),u),c[j])
    return '|'.join(c)
def table(s,header_start,cols,newheader=None):
    i=s.index(header_start); L=s[i:].split('\n'); n=0
    while n<len(L) and L[n].startswith('|'): n+=1
    out=[newheader or L[0],L[1]]+[cvcols(l,cols) for l in L[2:n]]
    return s[:i]+'\n'.join(out)+'\n'+'\n'.join(L[n:])
def rep(s,a,bb):
    assert a in s,a[:70]; return s.replace(a,bb)
