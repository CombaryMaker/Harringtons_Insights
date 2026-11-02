#!/bin/bash
# ticks.sh - thinned clock ticks on the desktop: all ticks vs ticks only when a shard's clock is >= 10 ms or 1 ms behind.
# Online lookups off (offline.zeek), max_timer_expires = 0. Every log compared with a plain run. MIT License.
W=$(pwd); R="$W/results_ticks.txt"; ZK=/opt/zk
h=$(sha256sum hs_pcapsplit_v2 | cut -c1-64)
[ "$h" = "$(cat hs_pcapsplit_v2.sha256)" ] || { echo "STOP: hs_pcapsplit_v2 hash $h" >> "$R"; echo DONE >> "$R"; exit 1; }
cp hs_pcapsplit_v2 $ZK/tools/hs_pcapsplit2 && chmod +x $ZK/tools/hs_pcapsplit2
echo "hs_pcapsplit_v2 verified" >> "$R"
for tk in "--ticks" "--ticks=10000" "--ticks=1000"; do
  sed "s|\"\$ZK/tools/hs_pcapsplit\" big8m.pcap \$k \"\$W/shards\" --ticks|\"\$ZK/tools/hs_pcapsplit2\" big8m.pcap \$k \"\$W/shards\" $tk|" $ZK/run_pc_offline.sh > $ZK/run_ticks.sh
  grep -q hs_pcapsplit2 $ZK/run_ticks.sh || { echo "STOP: script patch failed" >> "$R"; echo DONE >> "$R"; exit 1; }
  D=/root/zt_t${tk#--ticks}; mkdir -p $D && ln -sf /root/zt/big8m.pcap $D/big8m.pcap
  echo "==== tick setting: $tk (=N: tick only when the shard clock is N microseconds or more behind)" >> "$R"
  cd "$W"; RESULTS="$R" bash $ZK/run_ticks.sh $D "2 4 6 8 12"; rm -rf $D/k*
done
