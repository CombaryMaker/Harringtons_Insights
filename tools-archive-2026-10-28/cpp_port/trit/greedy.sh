#!/bin/bash
# greedy per-digit search for one file, two passes
f=$1; best=000000; bestv=$(./gtree tern6:$best $f | tail -1 | awk '{print $3}')
b8=$(./gtree bin8 $f | tail -1 | awk '{print $3}')
for pass in 1 2; do for d in 0 1 2 3 4 5; do for v in 0 1 2; do cand=$(echo $best | sed "s/./$v/$((d+1))"); [ "$cand" = "$best" ] && continue; r=$(./gtree tern6:$cand $f | tail -1 | awk '{print $3}'); if [ $r -lt $bestv ]; then best=$cand; bestv=$r; fi; done; done; done
echo "$(basename $f): bin8 $b8  best tern6:$best $bestv  diff $((bestv-b8))"
