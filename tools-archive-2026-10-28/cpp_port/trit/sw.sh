#!/bin/bash
# usage: sw.sh "-DFLAGS..."   prints text8 / all11 per-million and times
cd /tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cpp_port/trit
source files.sh
SRC=${SRC:-ptr2_bench.cpp}
name=$(echo "$1" | tr -d ' -' | tr '=' '_')
clang++ -O2 -ffp-contract=off -Wno-unused-function -Wno-unused-label -Wno-unused-variable $1 -o bin/sw_$name $SRC 2>&1 | grep error
bin/sw_$name $F > out/sw_$name.out
python3 - "$1" out/sw_$name.out <<'PY'
import sys,re
B={};tot=''
for l in open(sys.argv[2]):
    m=re.match(r'(\S+)\s+(\d+) -> +(\d+)',l)
    if m and m.group(1)!='TOTAL': B[m.group(1)]=(int(m.group(2)),int(m.group(3)))
    if l.startswith('TOTAL'): tot=l.split('per-million')[1].strip()
text=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','lcet10.txt','plrabn12.txt','xargs.1']
ti=sum(B[f][0] for f in text); to=sum(B[f][1] for f in text)
print(f"{sys.argv[1]:40s} text8 {to*1e6/ti:8.0f}  all11 {tot}  ken {B['kennedy.xls'][1]} ptt5 {B['ptt5'][1]} sum {B['sum'][1]}")
PY
