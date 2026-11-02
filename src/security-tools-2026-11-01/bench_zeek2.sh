# bench_zeek2.sh - Zeek 8.0.4: plain vs Harrington Sets shards (C++ splitter + merge), 2-core cloud. MIT License.
# Times include splitting and merging. Every log compared with zeekcmp.py.
SEC=$1; Z=$SEC/zeek/bin/zeek; P=$SEC/pc/big8m.pcap; W=$SEC/w/zk2; export LD_LIBRARY_PATH=$SEC/prefix/lib
el() { echo "$(date +%s.%N) - $1" | bc; }
[ -n "$SKIPPLAIN" ] || for r in 1 2; do mkdir -p $W/plain$r; cd $W/plain$r; s=$(date +%s.%N); $Z -C -r $P local >/dev/null 2>&1; echo "plain zeek run $r: $(el $s) s"; done
[ -n "$SKIPPLAIN" ] || echo "plain run 1 vs run 2: $(python3 -I $SEC/tools/zeekcmp.py $W/plain1 $W/plain2 | tail -1)"
for k in ${KS:-2 4 8}; do
  D=$W/split$k; mkdir -p $D/shards; s=$(date +%s.%N)
  $SEC/tools/hs_pcapsplit $P $k $D/shards >/dev/null; t1=$(date +%s.%N)
  for i in $(seq 0 $((k-1))); do f=$(printf "%02d" $i); mkdir -p $D/s$f; (cd $D/s$f && $Z -C -r $D/shards/shard$f.pcap local >/dev/null 2>&1) & done; wait; t2=$(date +%s.%N)
  rm -r "$D/shards"; python3 -I $SEC/tools/hs_zeek_merge.py $D/merged $D/s0*
  echo "split $k shards: $(el $s) s total (split $(echo "$t1-$s"|bc) s, zeek $(echo "$t2-$t1"|bc) s, merge $(el $t2) s)"
  python3 -I $SEC/tools/zeekcmp.py $W/plain1 $D/merged | grep -v IDENTICAL
done
