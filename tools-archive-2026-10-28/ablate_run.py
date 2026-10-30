import sys, subprocess, json
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad')
import hcm_ablate as H
which=sys.argv[1]
data = open('/usr/lib/python3.12/pydoc_data/topics.py','rb').read()[:200000] if which=='text' else \
  subprocess.run(['node','/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/gate_gen.js'],capture_output=True).stdout[:140000]
base=dict(tbits=22, match=True, verify=False, word=True, apm=True, apm_w=0.75, learn=True, lr=0.008)
tests=[('Full model (reference)',{}),('Remove long match',{'match':False}),('Remove word context',{'word':False}),
 ('Remove refinement stage',{'apm':False}),('Remove mixer learning',{'learn':False}),
 ('Refinement blend 50/50',{'apm_w':0.5}),('Refinement blend 90/10',{'apm_w':0.9}),
 ('Tables 2^20',{'tbits':20}),('Tables 2^18',{'tbits':18}),('Verified matches',{'verify':True})]
for name,ch in tests:
    H.CFG.clear(); H.CFG.update(base); H.CFG.update(ch)
    n=len(H.compress(data))
    print(json.dumps([which,name,n]),flush=True)
