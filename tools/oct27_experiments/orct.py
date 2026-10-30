import subprocess,os,sys
sys.path.insert(0,'../qx'); from var2 import rec
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
FILES=[('Canterbury',S+'/cant_dl/canterbury-corpus/canterbury/'+f) for f in ('kennedy.xls','ptt5','sum')]+[('Silesia',S+'/sil/f/'+f) for f in ('mr','x-ray','sao','osdb')]
def run(p,R,c):
    return [float(x) for x in subprocess.run(['./ob',p,str(R),f'w/{os.path.basename(p)}.{c}{R}.orc',c],capture_output=True,text=True,check=True).stdout.split()]
for c in ('zlib','zstd','snappy'):
    print('== codec',c,flush=True)
    for corp,p in FILES:
        R=max(rec(open(p,'rb').read()),2); U,b,tb=run(p,1,c); _,u,tu=run(p,R,c); m=lambda x:round(x/U*1e6)
        print(f'{corp} {os.path.basename(p)} R={R}: 1 column {m(b)} ({tb:.1f} ms) -> {R} columns {m(u)} ({tu:.1f} ms)',flush=True)
