#!/bin/bash
source files.sh
f="$1"; SRC=${SRC:-engine10.cpp}; name=$(echo "$f" | tr -d ' -' | tr '=' '_')
clang++ -O2 -ffp-contract=off -DCUMHASH=1 $f -o /tmp/sw2_$name $SRC 2>&1 | grep error
/tmp/sw2_$name $FILES > /tmp/sw2_$name.out
python3 - "$f" /tmp/sw2_$name.out <<'PY'
import sys,re
B={}
for l in open(sys.argv[2]):
    m=re.match(r'(\S+)\s+(\d+) -> +(\d+)',l)
    if m and m.group(1)!='TOTAL': B[m.group(1)]=int(m.group(3))
text=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','lcet10.txt','plrabn12.txt','xargs.1']
print(f"{sys.argv[1]:55s} text8 {sum(B[f] for f in text):7d}  all11 {sum(B.values()):7d}  alice {B['alice29.txt']}  lcet {B['lcet10.txt']}  plr {B['plrabn12.txt']}")
PY
