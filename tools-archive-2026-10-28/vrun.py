import sys, subprocess, importlib
sys.path.insert(0,'.')
mod=importlib.import_module(sys.argv[1])
text=open('/usr/lib/python3.12/pydoc_data/topics.py','rb').read()[:200000]
gate=subprocess.run(['node','gate_gen.js'],capture_output=True).stdout[:140000]
for nm,d in (('text',text),('gate',gate)):
    b=mod.compress(d); print(sys.argv[1], nm, len(b), mod.decompress(b)==d if nm=='gate' else '', flush=True)
