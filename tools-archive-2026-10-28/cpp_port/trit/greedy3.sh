#!/bin/bash
f=$1; best=${2:-000000}; A=${3:-510}; bestv=$(./trit_bench_x $best $A $f | tail -1 | awk '{print $4}')
for pass in 1 2; do for d in 0 1 2 3 4 5; do for v in 0 1 2; do cand=$(echo $best | sed "s/./$v/$((d+1))"); [ "$cand" = "$best" ] && continue; r=$(./trit_bench_x $cand $A $f | tail -1 | awk '{print $4}'); if [ $r -lt $bestv ]; then best=$cand; bestv=$r; fi; done; done; done
echo "$(basename $f) xorarg $A: best $best $bestv"
