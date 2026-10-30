import sys, os, json, time
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,S)
import hcm_v as H
C=S+'/cant_dl/canterbury-corpus/canterbury'
BASE=dict(tbits=22, match=True, verify=True, word=True, apm=True, apm_w=0.9, learn=True, lr=0.008, orders=(1,2,3,4,6,8), word2=True, limit_hi=255)
for name in sys.argv[1:]:
    data=open(f'{C}/{name}','rb').read()
    H.CFG.clear(); H.CFG.update(BASE)
    t=time.perf_counter(); blob=H.compress(data); te=time.perf_counter()-t
    ok=H.decompress(blob)==data
    print(json.dumps(dict(file=name,hcm2=len(blob),ok=ok,enc=round(te,1))),flush=True)
