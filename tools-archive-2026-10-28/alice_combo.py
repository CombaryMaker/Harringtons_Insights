import sys, json
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,S)
import hcm_v as H
data=open(S+'/cant_dl/canterbury-corpus/canterbury/alice29.txt','rb').read()
BASE=dict(tbits=22, match=True, verify=False, word=True, apm=True, apm_w=0.75, learn=True, lr=0.008, orders=(1,2,3,4,6), word2=False, limit_hi=255)
combos={'A':('Word pair + orders 1,2,3,4,6,8 + verified matches + 90/10',dict(word2=True,orders=(1,2,3,4,6,8),verify=True,apm_w=0.9)),
        'B':('Word pair + orders 1,2,3,4,5,6,8 + verified matches + 90/10',dict(word2=True,orders=(1,2,3,4,5,6,8),verify=True,apm_w=0.9))}
name,cfg=combos[sys.argv[1]]
H.CFG.clear(); H.CFG.update(BASE); H.CFG.update(cfg)
b=H.compress(data); ok=H.decompress(b)==data
print(json.dumps([name,len(b),ok]),flush=True)
