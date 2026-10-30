import json,sys
M='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/modern'
D=[json.loads(l) for l in open(M+'/results.jsonl')]
BASES=['gzip -9','bzip2 -9','xz -9e','zstd -19','zstd -22','lz4 -12']
HCM=['HCM fast (hcmx)','HCM max (hcmx)']
NP=['zstd-hi (vs zstd -22)','xz-hi (vs xz -9e)']
MUT=['record delta + zstd -19','record delta + xz -9e','activity map + gzip -9','activity map + zstd -19']
SHORT={'zstd-hi (vs zstd -22)':'zstd-hi','xz-hi (vs xz -9e)':'xz-hi','HCM fast (hcmx)':'HCM fast','HCM max (hcmx)':'HCM max',
 'record delta + zstd -19':'RD + zstd -19','record delta + xz -9e':'RD + xz -9e','activity map + gzip -9':'AM + gzip -9','activity map + zstd -19':'AM + zstd -19'}
sh=lambda n: SHORT.get(n,n)
pc=lambda a,b: f"{(a-b)/b*100:+.1f}%"
def fmt(n): return f"{n:,}"
out=[]
P=out.append
# all-ok check
bad=[(d['file'],k) for d in D for k,v in d['res'].items() if not v['ok']]
# --- size table
cols=BASES+HCM+NP
P('| File (bytes; R) | '+' | '.join(sh(c) for c in cols)+' |'); P('|---'*(len(cols)+1)+'|')
for d in D:
    r=d['res']; m=min(r[c]['size'] for c in cols if not r[c].get('fallback'))
    cells=[]
    for c in cols:
        s=fmt(r[c]['size'])
        if c in NP and r[c].get('fallback'): s+=' (fb)'
        cells.append(f"**{s}**" if r[c]['size']==m else s)
    P(f"| {d['file']} ({fmt(d['orig'])}; {d['R']}) | "+' | '.join(cells)+' |')
T={c:sum(d['res'][c]['size'] for d in D) for c in cols}; O=sum(d['orig'] for d in D)
P(f"| **Total ({fmt(O)})** | "+' | '.join(f"**{fmt(T[c])}**" for c in cols)+' |')
P('')
P('##RATIO')
P('| File | '+' | '.join(sh(c) for c in cols)+' |'); P('|---'*(len(cols)+1)+'|')
for d in D:
    P(f"| {d['file']} | "+' | '.join(f"{d['orig']/d['res'][c]['size']:.2f}" for c in cols)+' |')
P(f"| **Total** | "+' | '.join(f"**{O/T[c]:.2f}**" for c in cols)+' |')
P('')
# --- decode table: time, throughput, CPU, peak MB
P('##DEC')
allc=BASES+HCM+NP
P('| File | '+' | '.join(sh(c) for c in allc)+' |'); P('|---'*(len(allc)+1)+'|')
for d in D:
    cells=[]
    for c in allc:
        v=d['res'][c]; t=v['dec_s']; cells.append(f"{t*1000:,.1f} ms · {d['orig']/t/1e6:,.0f} MB/s · {v['dec_mb']:.0f} MB")
    P(f"| {d['file']} | "+' | '.join(cells)+' |')
P('')
P('##ENC')
P('| File | '+' | '.join(sh(c) for c in allc)+' |'); P('|---'*(len(allc)+1)+'|')
for d in D:
    cells=[]
    for c in allc:
        v=d['res'][c]; cells.append(f"{v['enc_s']:,.2f} s · {d['orig']/v['enc_s']/1e6:,.1f} MB/s · {v['enc_mb']:.0f} MB")
    P(f"| {d['file']} | "+' | '.join(cells)+' |')
P('')
P('##ENERGY')
P('| Method | Total encode CPU (s) | Encode energy (J) | Total decode CPU (s) | Decode energy (J) |'); P('|---|---|---|---|---|')
for c in allc:
    ec=sum(d['res'][c]['enc_cpu'] for d in D); dc=sum(d['res'][c]['dec_cpu'] for d in D)
    P(f"| {sh(c)} | {ec:,.2f} | {ec*15:,.1f} | {dc:,.3f} | {dc*15:,.2f} |")
P('')
P('##NOPAIN')
P('| File | xz-hi vs xz -9e: bytes | size | decode | verdict | zstd-hi vs zstd -22: bytes | size | decode | verdict |'); P('|---|---|---|---|---|---|---|---|---|')
for d in D:
    r=d['res']; cells=[]
    for n,b in (('xz-hi (vs xz -9e)','xz -9e'),('zstd-hi (vs zstd -22)','zstd -22')):
        a=r[n]; bb=r[b]
        v='no gain' if a['size']==bb['size'] else ('fallback' if a.get('fallback') else 'No-Pain')
        cells+= [f"{fmt(bb['size'])} → {fmt(a['size'])}",pc(a['size'],bb['size']),f"{bb['dec_s']*1000:.1f} → {a['dec_s']*1000:.1f} ms",v]
    P(f"| {d['file']} | "+' | '.join(cells)+' |')
P('')
P('##MUT')
P('| File (R) | Method | Bytes | vs base | Decode | Base decode | Verdict |'); P('|---|---|---|---|---|---|---|')
for d in D:
    r=d['res']
    for n in MUT:
        if n not in r: continue
        base=n.split('+ ')[1]; a=r[n]; bb=r[base]
        v='No-Pain' if a['size']<bb['size'] and a['dec_s']<=bb['dec_s'] else ('smaller, slower' if a['size']<bb['size'] else 'larger')
        P(f"| {d['file']} ({d['R']}) | {sh(n)} | {fmt(a['size'])} | {pc(a['size'],bb['size'])} | {a['dec_s']*1000:.1f} ms | {bb['dec_s']*1000:.1f} ms | {v} |")
P('')
P('##SUM')
for d in D:
    r=d['res']; stock=min(BASES,key=lambda c:r[c]['size']); h=r['HCM max (hcmx)']
    P(f"{d['file']}: best stock {stock} {r[stock]['size']}; HCM max {h['size']} ({pc(h['size'],r[stock]['size'])}), dec {h['dec_s']:.2f}s vs {r[stock]['dec_s']*1000:.1f}ms ({h['dec_s']/r[stock]['dec_s']:.0f}x); xz-hi {pc(r['xz-hi (vs xz -9e)']['size'],r['xz -9e']['size'])} dec {pc(r['xz-hi (vs xz -9e)']['dec_s'],r['xz -9e']['dec_s'])} fb={r['xz-hi (vs xz -9e)'].get('fallback',False)}; zhi {pc(r['zstd-hi (vs zstd -22)']['size'],r['zstd -22']['size'])} fb={r['zstd-hi (vs zstd -22)'].get('fallback',False)}")
P('BAD '+str(bad))
print('\n'.join(out))
