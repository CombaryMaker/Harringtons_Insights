#!/bin/bash
# usage: b3.sh SRC "FLAGS" -> best-of-3 totals
source files.sh
clang++ -O2 -ffp-contract=off $2 -o /tmp/b3bin $1 2>&1 | grep error
best_c=999; best_d=999; line=""
for r in 1 2 3; do
  out=$(/tmp/b3bin $FILES | tail -1)
  c=$(echo "$out" | sed -E 's/.*compress ([0-9.]+)s.*/\1/'); d=$(echo "$out" | sed -E 's/.*decompress ([0-9.]+)s.*/\1/')
  best_c=$(python3 -c "print(min($best_c,$c))"); best_d=$(python3 -c "print(min($best_d,$d))"); line=$out
done
pm=$(echo "$line" | sed -E 's/.*per-million ([0-9]+).*/\1/'); ok=$(echo "$line" | sed -E 's/.*all-ok ([0-9]).*/\1/')
echo "$1 $2 | per-million $pm | best compress ${best_c}s decompress ${best_d}s | ok $ok"
