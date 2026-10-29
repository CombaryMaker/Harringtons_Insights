import subprocess, time, os, filecmp
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; W=S+'/h36s'
for f in ['samba','mozilla']:
    src=f'{S}/sil/f/{f}'; o=f'{W}/{f}.hs'
    env=dict(os.environ, HCM_VERBOSE='1')
    a=time.perf_counter(); r=subprocess.run([S+'/hcm36','s',src,o],capture_output=True,env=env); ct=time.perf_counter()-a
    a=time.perf_counter(); subprocess.run([S+'/hcm36','d',o,o+'.out'],capture_output=True); dt=time.perf_counter()-a
    ok=filecmp.cmp(src,o+'.out',shallow=False); os.remove(o+'.out')
    print(f'{f:8s} v3.6 sets {os.path.getsize(o):>12,} bytes  compress {ct:7.1f} s  decode {dt:7.1f} s  exact={ok}')
    print('\n'.join(l for l in r.stderr.decode().splitlines() if 'set' in l or 'windows' in l), flush=True)
