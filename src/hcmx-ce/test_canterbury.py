import subprocess,os,time,glob,filecmp,statistics
C='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cant_dl/canterbury-corpus/canterbury/'
files=sorted(f for f in glob.glob(C+'*') if 'SHA1' not in f)
def run(cmd,env):
    t=time.perf_counter(); subprocess.run(cmd,env=env,capture_output=True,check=True); return time.perf_counter()-t
V=[('v3.3 (no pointer)','/tmp/claude-0/hcmx0',32,None),('ternary pointer','/tmp/claude-0/hcmx0',96,None),('candidate estimator','/tmp/claude-0/hcmxce',96,'1')]
BIN={'kennedy.xls','ptt5','sum'}
tot={}
for name,b,only,ce in V:
    S=0;CT=0;DT=0
    for f in files:
        env=dict(os.environ,HCMX_ONLY=str(only|(16 if os.path.basename(f) in BIN else 0)))
        if ce: env['HCMX_CE']='1'
        o='/tmp/claude-0/ce/'+os.path.basename(f)+'.z'
        ct=run([b,'c',f,o],env); S+=os.path.getsize(o)
        d=[run([b,'d',o,o+'.o'],env) for _ in range(5)]; DT+=statistics.median(d); CT+=ct
        assert filecmp.cmp(f,o+'.o',shallow=False), (name,f)
        print(name,os.path.basename(f),os.path.getsize(f),os.path.getsize(o),f'{ct:.2f}',f'{statistics.median(d):.3f}',flush=True)
    tot[name]=(S,CT,DT)
for k,v in tot.items(): print(f'TOTAL {k:22s} {v[0]:>9,} bytes  1M bits-> {v[0]/2810784*1e6:,.0f}  compress {v[1]:.2f} s  decode {v[2]:.2f} s')
