#!/bin/bash
# Full fair run: every file x every coder, one at a time, single thread. Results: bench.jsonl
B=/tmp/claude-0/meshbench/c/meshbench2; OUT=/tmp/claude-0/meshbench/c/bench.jsonl; : > $OUT
CODERS="stlx3+zstd19 stlx3+xz stlx3+zpaq5 meshopt+zstd19 meshopt+xz meshopt+zpaq5 openctm-mg1 draco-qp0 raw+zstd19 raw+xz raw+zpaq5"
for f in /tmp/claude-0/realstl/set/*.stl /tmp/claude-0/stl/set/*.stl; do
  sz=$(stat -c%s "$f")
  $B load "$f" 1 | sed "s|}|,\"file\":\"$f\"}|" >> $OUT
  for c in $CODERS; do
    runs=5; [ $sz -gt 20000000 ] && runs=3; case $c in *zpaq5) runs=3; [ $sz -gt 20000000 ] && runs=1;; esac
    timeout 3000 $B $c "$f" $runs >> $OUT || echo "{\"coder\":\"$c\",\"file\":\"$f\",\"error\":true}" >> $OUT
  done
done
echo finished
