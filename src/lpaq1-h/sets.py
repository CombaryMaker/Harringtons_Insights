# lpaq1-h on Andrew Goodwin's three benchmark sets: size, compression time (all 6 flag tries), decode time; every round trip checked.
import subprocess as sp,os,glob,time,json
L='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/lpaq'
sets={'Canterbury':[f for f in sorted(glob.glob('/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cant_dl/canterbury-corpus/canterbury/*')) if 'SHA1' not in f],
      'Silesia (first 1 MB of each file)':sorted(glob.glob(L+'/s1m/*')),
      'Synthetic':sorted(glob.glob('/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/fix/syn/d/*'))}
out={}
for name,files in sets.items():
    tot=dict(orig=0,comp=0,ct=0.0,dt=0.0); per={}
    for f in files:
        best=None; ct=0.0
        for fl in (0,4,9,13,11,15):
            t=time.perf_counter(); sp.run([L+'/lpaq1h','6',f,L+'/b.c'],capture_output=True,check=True,env=dict(os.environ,LPH_FLAGS=str(fl))); ct+=time.perf_counter()-t
            b=os.path.getsize(L+'/b.c')
            if best is None or b<best[0]: best=(b,fl); os.replace(L+'/b.c',L+'/best.c')
        t=time.perf_counter(); sp.run([L+'/lpaq1h','d',L+'/best.c',L+'/o.d'],capture_output=True,check=True); dt=time.perf_counter()-t
        assert open(L+'/o.d','rb').read()==open(f,'rb').read(), f
        o=os.path.getsize(f); per[os.path.basename(f)]=dict(orig=o,comp=best[0],flags=best[1],ct=round(ct,3),dt=round(dt,3))
        tot['orig']+=o; tot['comp']+=best[0]; tot['ct']+=ct; tot['dt']+=dt
    out[name]=dict(total=tot,files=per); print(name,tot,flush=True)
json.dump(out,open(L+'/lpaq1h_goodwin_sets.json','w'),indent=1)
