# Convert "per 1,000,000" table cells to bytes in markdown tables.
import os,re,sys
S=os.path.dirname(os.path.abspath(__file__))
U={}
for d in (S+'/cant_dl/canterbury-corpus/canterbury',S+'/sil/f'):
    for f in os.listdir(d):
        if f!='SHA1SUM': U[f]=os.path.getsize(d+'/'+f)
CANT=sum(U[f] for f in ['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1'])
SIL=sum(v for k,v in U.items() if k in ['dickens','mozilla','mr','nci','ooffice','osdb','reymont','samba','sao','webster','x-ray','xml'])
NUM=re.compile(r'(?<![\d.])(\d{1,3}(?:,\d{3})+|\d{4,6})(?![\d.,]*\s*(?:ms|µs|s\b))')
def conv(v,u):
    b=v*u/1e6
    if u<=2_100_000: return f"{round(b):,}"          # exact: error < 1 byte
    return "≈"+f"{round(b,-2):,.0f}"
def size_of(key,ctx):
    k=key.strip('* ').split(' (')[0].strip()
    if k in U: return U[k]
    if k.lower().startswith('total'):
        if 'sil' in k.lower() or ctx=='sil': return SIL
        return CANT
    return None
def row(line,ctx):
    cells=line.split('|')
    u=size_of(cells[1],ctx)
    if u is None: return line,ctx
    if u in (U.get('dickens'),): ctx='sil'
    out=[cells[0],cells[1]]
    for c in cells[2:]:
        parts=c.split(' / ')  # strip times after " / "
        # convert only numbers not after "/" (time) and not inside backticks
        if '`' in c or 'decode' in c: 
            head=re.split(r'\(decode',c)
            head[0]=NUM.sub(lambda m: conv(int(m.group(1).replace(',','')),u) if not m.group(1).isdigit() or len(m.group(1))>=5 else m.group(0),head[0]) if '`' not in c else head[0]
            out.append('(decode'.join(head)); continue
        segs=re.split(r'( / [\d.,]+)',c)
        segs=[s if s.startswith(' / ') else NUM.sub(lambda m: conv(int(m.group(1).replace(',','')),u),s) for s in segs]
        out.append(''.join(segs))
    return '|'.join(out),ctx
if __name__=='__main__':
    print('CANT',CANT,'SIL',SIL)

CF=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
def n(s): return int(s.replace(',','').replace('≈','').replace('*',''))
def fix_cant_totals(lines):
    """Replace ≈ Canterbury totals with exact sums of the converted file rows."""
    out=[];rows=[]
    for l in lines:
        c=[x.strip() for x in l.split('|')] if l.startswith('|') else None
        if c and c[1].strip('* ') in CF: rows.append(c)
        if c and c[1].strip('* ').lower().startswith('total') and rows and 'sil' not in c[1].lower() and len(rows)==11:
            cells=l.split('|')
            for j in range(2,len(cells)-1):
                cell=cells[j]
                if '≈' not in cell: continue
                A=B=0; T=None
                for r in rows:
                    v=r[j]
                    if v.startswith('`') : break
                    m=re.findall(r'\d{1,3}(?:,\d{3})+|\d+',v.split(' / ')[0])
                    if 'no gain' in v: a=b=n(r[2])
                    else: a=n(m[0]); b=n(m[1]) if '→' in v else a
                    A+=a;B+=b
                else:
                    t=cell.split(' / ')
                    tail=(' / '+t[1].strip('* ').strip()) if len(t)>1 else ''
                    val=f"{A:,} → {B:,}" if '→' in cell else f"{A:,}"
                    cells[j]=f" **{val}{tail}** "
            l='|'.join(cells); rows=[]
        out.append(l)
    return out
