#!/bin/bash
# run_par.sh - Zeek with Harrington's Sets, streaming mode, on the desktop (runs inside the ZeekTest WSL Linux).
# Michael Hugh Harrington, 2026. MIT License.
# hs_zeek_par plans the sets in memory and feeds every Zeek through a pipe (no shard files); clock ticks only when a
# shard's clock is 10 ms or more behind; the merge runs one log per core. Every run: max_timer_expires = 0 and Zeek's
# online malware-hash lookups off, in the plain run too. Every log compared row by row with the plain run.
# Two capture sizes: the 903 MB test capture and a 7.2 GB capture (same public base, 64 copies).
W=$(pwd); R="$W/results_par.txt"; ZK=/opt/zk; . $ZK/env.sh
log() { echo "$*" | tee -a "$R"; }
el() { python3 -c "import sys;print(f'{float(sys.argv[2])-float(sys.argv[1]):.2f}')" "$1" "$(date +%s.%N)"; }
cd "$W/par" && sha256sum -c SHA256SUMS --quiet || { log "STOP: file check failed"; log DONE; exit 1; }
cp hs_zeek_par bigcapn $ZK/tools/ && cp hs_zeek_merge_par.py $ZK/tools/ && chmod +x $ZK/tools/hs_zeek_par $ZK/tools/bigcapn
log "== streaming test, files verified, $(date)"
log "machine: $(nproc) CPUs visible ($(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')), RAM $(free -g | awk '/Mem/{print $2}') GB"
B=/root/big64.pcap
if [ ! -f $B ]; then $ZK/tools/bigcapn $ZK/base_m.pcap $B 64 40000000 2>/dev/null; fi
h=$(sha256sum $B | cut -c1-64); [ "$h" = ec1a5ca8644a1cc94367e820f8820a6ab0ac0c461d159b98ab8552ad513a40e8 ] || { log "STOP: 7.2 GB capture hash $h"; log DONE; exit 1; }
h=$(sha256sum /root/zt/big8m.pcap | cut -c1-64); [ "$h" = b3db1ba84a0134b57f3ee2192ca2c0bbe3ee241223542a89006f9e8c2cfabfaa ] || { log "STOP: 903 MB capture hash $h"; log DONE; exit 1; }
log "captures verified: big8m.pcap $(stat -Lc%s /root/zt/big8m.pcap) bytes and big64.pcap $(stat -Lc%s $B) bytes (SHA-256 match the cloud)"
OFF="$ZK/tools/notimerthrottle.zeek $ZK/tools/offline.zeek"
for cap in /root/zt/big8m.pcap $B; do
  T=/root/par_$(basename $cap .pcap); rm -rf $T; mkdir -p $T/plain; cd $T/plain
  log ""; log "==== $(basename $cap) ($(stat -Lc%s $cap) bytes)"
  s=$(date +%s.%N); $ZEEK -C -r $cap local $OFF >/dev/null 2>&1; log "plain zeek: $(el $s) s"
  for k in ${KS:-4 6 8 12}; do
    D=$T/k$k; s=$(date +%s.%N)
    out=$($ZK/tools/hs_zeek_par $cap $k $D/run 10000 -- $ZEEK local $ZK/tools/hs_shard.zeek $OFF); t1=$(date +%s.%N)
    python3 -I $ZK/tools/hs_zeek_merge_par.py $D/merged $D/run/s[0-9]*
    log "$k pieces: total $(el $s) s (plan+zeek $(python3 -c "print(f'{$t1-$s:.2f}')"), merge $(el $t1))"
    echo "$out" | sed 's/^/    /' | tee -a "$R" >/dev/null
    python3 -I $ZK/tools/zeekcmp.py $T/plain $D/merged | grep -v IDENTICAL | sed 's/^/    /' | tee -a "$R"
    rm -rf $D/run
  done
done
log "== finished $(date)"; log DONE
