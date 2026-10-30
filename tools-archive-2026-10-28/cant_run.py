import sys, os, json, time, lzma, bz2, subprocess
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,S)
import hcm_context_balance as H
C=S+'/cant_dl/canterbury-corpus/canterbury'; os.makedirs(S+'/cant_out',exist_ok=True)
for name in sys.argv[1:]:
    data=open(f'{C}/{name}','rb').read()
    a=f'{S}/cant_out/{name}.zpaq'
    if os.path.exists(a): os.remove(a)
    subprocess.run([S+'/zpaqbin','a',a,f'{C}/{name}','-m5'],capture_output=True)
    t=time.perf_counter(); blob=H.compress(data); te=time.perf_counter()-t
    open(f'{S}/cant_out/{name}.hcm','wb').write(blob)
    t=time.perf_counter(); ok=H.decompress(open(f'{S}/cant_out/{name}.hcm','rb').read())==data; td=time.perf_counter()-t
    print(json.dumps(dict(file=name,size=len(data),hcm=len(blob),ok=ok,enc=round(te,1),dec=round(td,1),
        xz=len(lzma.compress(data,preset=9|lzma.PRESET_EXTREME)),bz2=len(bz2.compress(data,9)),zpaq=os.path.getsize(a))),flush=True)
