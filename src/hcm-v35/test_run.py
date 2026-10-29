import subprocess, time, os, filecmp, sys
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; W=S+'/h35s'; F=S+'/sil/f'
def t(cmd, env=None):
    a=time.perf_counter(); subprocess.run(cmd, capture_output=True, env=env); return time.perf_counter()-a
for f in ['samba','mozilla']:
    src=os.path.join(F,f)
    for tag, exe, cmd, env in (('v3.4', S+'/fix/hcm34b', 'c', None), ('v3.5', S+'/hcm35', 'c', None),
                               ('v3.5 parallel 16MB', S+'/hcm35', 'p', dict(os.environ, HCM_BLOCK_MB='16'))):
        o=f'{W}/{f}.{cmd}{tag[-3:].strip()}'
        ct=t([exe, cmd, src, o], env)
        dec=f'{o}.out'; dt=t([S+'/hcm35' if 'v3.5' in tag else exe, 'd', o, dec])
        ok=filecmp.cmp(src, dec, shallow=False); os.remove(dec)
        print(f'{f:8s} {tag:20s} {os.path.getsize(o):>12,} bytes  compress {ct:7.1f} s  decode {dt:7.1f} s  exact={ok}', flush=True)
