import os,sys,json,random,secrets,subprocess
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
sys.path.insert(0,S+'/qx'); from var2 import rec
C=S+'/cant_dl/canterbury-corpus/canterbury/'; SL=S+'/sil/f/'; MD=S+'/modern/data/'
pool=sorted([C+f for f in os.listdir(C) if f!='SHA1SUM']+[SL+f for f in os.listdir(SL)]+[MD+f for f in os.listdir(MD)])
seed=secrets.randbits(32); pick=random.Random(seed).sample(pool,8)
out=open('speed1.jsonl','a'); print('seed',seed)
for f in pick:
    R=max(rec(open(f,'rb').read()[:200000]),2)
    for codec,lvl in (('lz4',1),('zstd',1)):
        d=json.loads(subprocess.run(['./rvb',codec,str(lvl),f,str(R),'3'],capture_output=True,text=True,env=dict(os.environ,FASTU='1')).stdout)
        best=('plain',d['plain'],d['dec_plain_us'])
        for k in ('delta','act'):
            if d[k]<d['plain'] and d[f'dec_{k}_us']<=d['dec_plain_us'] and d[k]<best[1]: best=(k,d[k],d[f'dec_{k}_us'])
        r=dict(seed=seed,file=os.path.basename(f),R=R,codec=f'{codec} {lvl}',**d,pick=best[0]); out.write(json.dumps(r)+'\n')
        print(f"{os.path.basename(f):28s} R={R:<3} {codec}{lvl}: plain {d['plain']:>10,} {d['dec_plain_us']/1e3:7.2f}ms | delta {d['delta']:>10,} {d['dec_delta_us']/1e3:7.2f}ms | act {d['act']:>10,} {d['dec_act_us']/1e3:7.2f}ms -> {best[0]}",flush=True)
