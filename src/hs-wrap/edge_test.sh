#!/bin/bash
# Boundary round trips (30 Oct 2026): HCM modes c x p s t (HCM_BLOCK_MB=1) and hs_wrap with xz, block 1 MB.
# usage: edge_test.sh HCM_BINARY HS_WRAP_PY   (run in a folder holding the e_* test files made by make_edge.py)
H=$1; W=$2; bad=0; cnt=0
for f in e_*; do
  for m in c x p s t; do HCM_BLOCK_MB=1 $H $m $f o.$m >/dev/null 2>&1 && $H d o.$m r.$m >/dev/null 2>&1 && cmp -s $f r.$m || { echo "HCM FAIL $m $f"; bad=$((bad+1)); }; cnt=$((cnt+1)); rm -f o.$m r.$m; done
  python3 $W c $f w.hsw --comp "xz -6 -c {in}" --block-mb 1 >/dev/null 2>&1 && python3 $W d w.hsw w.out --decomp "xz -dc {in}" >/dev/null 2>&1 && cmp -s $f w.out || { echo "WRAP FAIL $f"; bad=$((bad+1)); }; cnt=$((cnt+1)); rm -f w.hsw w.out
done; echo "edge round trips: $cnt, failures: $bad"
