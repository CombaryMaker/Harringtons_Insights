import subprocess, re, os
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
C=S+'/cant_dl/canterbury-corpus/canterbury'; B=S+'/bz2lab'
FILES=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
def dect(path, n, reps):
    o=subprocess.run([B+'/bz2dec_bench',path,str(n),str(reps)],capture_output=True,text=True).stdout
    return float(re.search(r'median ([\d.]+) us',o).group(1))
tb=tn=sb=sn=0; tin=0
for fn in FILES:
    src=C+'/'+fn; n=os.path.getsize(src); tin+=n
    base=subprocess.run(['bzip2','-9','-c',src],capture_output=True).stdout
    open(B+'/'+fn+'.base.bz2','wb').write(base)
    reps=200 if n<200000 else 60
    bt=dect(B+'/'+fn+'.base.bz2',n,reps)
    cands=[]
    for lvl in range(9,0,-1):
        blob=subprocess.run([B+'/tuned/bzip2_tuned','-%d'%lvl,'-c',src],capture_output=True).stdout
        cands.append((len(blob),lvl,blob))
    cands.sort(key=lambda x:(x[0],-x[1]))
    chosen=(len(base),'stock -9',bt)
    for size,lvl,blob in cands:
        if size>len(base): break
        p=B+'/'+fn+'.new.bz2'; open(p,'wb').write(blob)
        ok=subprocess.run(f'bzip2 -dc {p} | cmp -s - {src}',shell=True).returncode==0
        # interleaved re-measurement: candidate must not be slower than stock (strict)
        tc=[];tbs=[]
        for _ in range(3): tbs.append(dect(B+'/'+fn+'.base.bz2',n,reps)); tc.append(dect(p,n,reps))
        t=min(tc); b2=min(tbs)
        if ok and t<=b2: chosen=(size,'tuned -%d'%lvl,t); bt=b2; break
    sb+=len(base); sn+=chosen[0]; tb+=bt; tn+=chosen[2]
    print(f"{fn:13s} bzip2 -9 {len(base):7d} ({bt:8.1f} us)  ->  {chosen[0]:7d} ({chosen[2]:8.1f} us)  {chosen[1]}")
print(f"ALL 11: 1,000,000 bits down to {sb*1e6/tin:.0f} -> {sn*1e6/tin:.0f}; decode {tb/1000:.1f} ms -> {tn/1000:.1f} ms")
