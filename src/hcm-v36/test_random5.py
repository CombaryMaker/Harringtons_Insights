import subprocess, time, os, filecmp, random, glob
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; W=S+'/h36r'
pool=[f for f in glob.glob(S+'/cant_dl/canterbury-corpus/canterbury/*')+glob.glob(S+'/sil/f/*') if 'SHA1' not in f and os.path.getsize(f)<=11_000_000]
seed=int(time.time()); random.seed(seed); pick=random.sample(sorted(pool),5)
print('seed',seed,'picked:',', '.join(os.path.basename(p) for p in pick), flush=True)
def run(cmd,env=None):
    a=time.perf_counter(); subprocess.run(cmd,capture_output=True,env=env); return time.perf_counter()-a
for src in pick:
    f=os.path.basename(src); n=os.path.getsize(src)
    for tag,cmd,env in (('v3.4 single','c',None),('v3.5 blocks 2MB','p',dict(os.environ,HCM_BLOCK_MB='2')),('v3.6 sets 2MB','s',dict(os.environ,HCM_BLOCK_MB='2'))):
        o=f'{W}/{f}.{cmd}'; ct=run([S+'/hcm36',cmd,src,o],env); dt=run([S+'/hcm36','d',o,o+'.out'])
        ok=filecmp.cmp(src,o+'.out',shallow=False); os.remove(o+'.out'); z=os.path.getsize(o)
        print(f'{f:13s} {n:>10,}  {tag:16s} {z:>10,} B  1M bits->{z/n*1e6:>8,.0f}  ratio {n/z:6.3f}  compress {ct:6.1f} s  decode {dt:6.1f} s  exact={ok}', flush=True)
