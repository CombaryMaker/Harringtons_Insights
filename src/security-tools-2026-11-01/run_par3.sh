#!/bin/bash
# run_par3.sh - Zeek with Harrington's Sets on the desktop, round 3 (inside the ZeekTest WSL Linux). MIT License.
# Michael Hugh Harrington, 2026.
# Change from round 2: the merge joins logs that need no per-host decisions (conn, dns, http, files, ...) shard by
# shard without parsing, as a Zeek cluster's logger does; only known_hosts, known_certs, known_services, software and
# notice are parsed. Plan-first runner (hs_zeek_par), 7.2 GB and 903 MB captures, online lookups off,
# max_timer_expires = 0 everywhere. Every log compared row by row with plain Zeek.
W=$(pwd); R="$W/results_par3.txt"; ZK=/opt/zk; . $ZK/env.sh
log() { echo "$*" | tee -a "$R"; }
el() { python3 -c "import sys;print(f'{float(sys.argv[2])-float(sys.argv[1]):.2f}')" "$1" "$(date +%s.%N)"; }
cd "$W/par3" && sha256sum -c SHA256SUMS --quiet || { log "STOP: file check failed"; log DONE; exit 1; }
cp hs_zeek_merge_par.py $ZK/tools/hs_zeek_merge_fast.py
log "== round 3, files verified, $(date)"
OFF="$ZK/tools/notimerthrottle.zeek $ZK/tools/offline.zeek"; SH="$ZK/tools/hs_shard.zeek"
for cap in /root/big64r.pcap /root/zt/big8m.pcap; do
  case $cap in *big64r*) want=93a45045d265854fc82b9947fc794663fe88d985ea8a96dfac7286a262a9f28e;; *) want=b3db1ba84a0134b57f3ee2192ca2c0bbe3ee241223542a89006f9e8c2cfabfaa;; esac
  h=$(sha256sum $cap | cut -c1-64); [ "$h" = "$want" ] || { log "STOP: $cap hash $h"; log DONE; exit 1; }
  T=/root/par3_$(basename $cap .pcap); rm -rf "${T:?}"; mkdir -p $T/plain; cd $T/plain
  log ""; log "==== $(basename $cap) ($(stat -Lc%s $cap) bytes, SHA-256 verified)"
  s=$(date +%s.%N); $ZEEK -C -r $cap local $OFF >/dev/null 2>&1; log "plain zeek: $(el $s) s"
  for k in 8 12; do
    D=$T/k$k; s=$(date +%s.%N)
    out=$($ZK/tools/hs_zeek_par $cap $k $D/run 10000 -- $ZEEK local $SH $OFF); t1=$(date +%s.%N)
    python3 -I $ZK/tools/hs_zeek_merge_fast.py $D/merged $D/run/s[0-9]*
    log "$k pieces: total $(el $s) s (plan+zeek $(python3 -c "print(f'{$t1-$s:.2f}')"), merge $(el $t1))"
    echo "$out" | sed 's/^/    /' >> "$R"
    python3 -I $ZK/tools/zeekcmp.py $T/plain $D/merged | grep -v IDENTICAL | sed 's/^/    /' | tee -a "$R"
    rm -rf "${D:?}/run"
  done
done
log "== finished $(date)"; log DONE
