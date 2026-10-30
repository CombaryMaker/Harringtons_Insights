#!/bin/bash
# usage: tm.sh bin1 bin2 ...   min compress/decompress CPU over 3 interleaved runs
cd /tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cpp_port/trit
source files.sh
for r in 1 2 3; do for b in "$@"; do echo "$b $($b $F | tail -1)"; done; done | python3 -c '
import sys,re
R={}
for l in sys.stdin:
    b=l.split()[0]; m=re.search(r"per-million (\d+) compress ([\d.]+)s decompress ([\d.]+)s all-ok (\d)",l)
    R.setdefault(b,[]).append((int(m.group(1)),float(m.group(2)),float(m.group(3)),m.group(4)))
for b,v in R.items(): print(f"{b:28s} {v[0][0]:7d}  compress {min(x[1] for x in v):.2f}s  decompress {min(x[2] for x in v):.2f}s  ok {v[0][3]}")
'
