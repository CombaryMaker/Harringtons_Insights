# bench_zeek3.sh - Zeek 8.0.4 Harrington Sets with clock ticks + raw-input replay merge; 2-core cloud. MIT License.
# Times include splitting and merging. Compared with the plain run (zk2/plain1) by zeekcmp.py.
SEC=$1; Z=$SEC/zeek/bin/zeek; P=$SEC/pc/big8m.pcap; W=$SEC/w/zk3; export LD_LIBRARY_PATH=$SEC/prefix/lib
el() { echo "$(date +%s.%N) - $1" | bc; }
for k in ${KS:-2 4 8}; do for tk in ${TKS:---ticks none}; do
  D=$W/k$k$tk${TAG}; mkdir -p $D/shards; s=$(date +%s.%N)
  m=$($SEC/tools/hs_pcapsplit $P $k $D/shards $tk); t1=$(date +%s.%N)
  for i in $(seq 0 $((k-1))); do f=$(printf "%02d" $i); mkdir -p $D/s$f; (cd $D/s$f && $Z -C -r $D/shards/shard$f.pcap local $SEC/tools/hs_shard.zeek $EXTRA >/dev/null 2>&1) & done; wait; t2=$(date +%s.%N)
  rm -r "$D/shards"; python3 -I $SEC/tools/hs_zeek_merge.py $D/merged $D/s0*
  echo "$k shards, ticks=$tk: $(el $s) s (split $(echo "$t1-$s"|bc), zeek $(echo "$t2-$t1"|bc), merge $(el $t2)) [$m]"
  python3 -I $SEC/tools/zeekcmp.py ${REF:-$SEC/w/zk2/plain1} $D/merged | grep -v IDENTICAL | sed 's/^/   /'
done; done
