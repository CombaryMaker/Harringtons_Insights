import sys, subprocess
sys.path.insert(0,'/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad')
import hcm_ablate as H
which=sys.argv[1]
data = open('/usr/lib/python3.12/pydoc_data/topics.py','rb').read()[:200000] if which=='text' else \
  subprocess.run(['node','/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/gate_gen.js'],capture_output=True).stdout[:140000]
H.CFG.clear(); H.CFG.update(dict(tbits=22, match=True, verify=True, word=True, apm=True, apm_w=0.9, learn=True, lr=0.008))
b=H.compress(data); ok=H.decompress(b)==data
print(which, len(b), ok, flush=True)
