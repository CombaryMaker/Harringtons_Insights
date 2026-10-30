#!/bin/bash
# text-tuning sweep on v3 engine: usage swt.sh "-DFLAGS"
cd /tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cpp_port/trit
source files.sh
SRC=${SRC:-txt_bench.cpp}
name=t$(echo "$*" | sed "s/[^A-Za-z0-9]/_/g")
clang++ -O2 -ffp-contract=off -Wno-unused-function -Wno-unused-label -Wno-unused-variable -DPTR=0 -DSWAPN=3 "$@" -o bin/$name $SRC 2>&1 | grep error
bin/$name $F > out/$name.out
python3 - "$*" out/$name.out <<'PY'
import sys,re
B={};tot=''
for l in open(sys.argv[2]):
    m=re.match(r'(\S+)\s+(\d+) -> +(\d+)',l)
    if m and m.group(1)!='TOTAL': B[m.group(1)]=(int(m.group(2)),int(m.group(3)))
    if l.startswith('TOTAL'): tot=l.split('per-million')[1].strip()
text=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','lcet10.txt','plrabn12.txt','xargs.1']
ti=sum(B[f][0] for f in text); to=sum(B[f][1] for f in text)
print(f"{sys.argv[1]:45s} text8 {to*1e6/ti:8.0f}  all11 {tot}  alice {B['alice29.txt'][1]} lcet {B['lcet10.txt'][1]} plr {B['plrabn12.txt'][1]} cp {B['cp.html'][1]}")
PY
