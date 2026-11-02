#!/bin/bash
# run_pc.sh - Zeek 8.0.4 exact-match test with Harrington's Sets on the desktop. Michael Hugh Harrington, 2026. MIT License.
# Rebuilds the same public 903 MB capture as the cloud test (checked by SHA-256), runs plain Zeek, then 2..12 shards with
# clock ticks and the replay merge; every log compared with the plain run. max_timer_expires = 0 in every run.
# Usage: bash run_pc.sh TESTDIR "2 4 6 8 12"
set -u; ZK=$(cd "$(dirname "$0")" && pwd); D=$1; KS=${2:-2 4 6 8 12}; . "$ZK/env.sh"; L="${RESULTS:-$D/results.txt}"; mkdir -p "$D"; cd "$D"
log() { echo "$*" | tee -a "$L"; }
el() { python3 -c "import sys;print(f'{float(sys.argv[2])-float(sys.argv[1]):.2f}')" "$1" "$(date +%s.%N)"; }
log "== Zeek exact-match desktop test, started $(date)"
log "machine: $(nproc) CPUs visible ($(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')), RAM $(free -g | awk '/Mem/{print $2}') GB"
if [ ! -f big8m.pcap ]; then mkdir -p src && cp "$ZK/base_m.pcap" src/ && "$ZK/tools/bigcap" src base_m.pcap big8m.pcap >/dev/null; fi
h=$(sha256sum big8m.pcap | cut -c1-64)
[ "$h" = b3db1ba84a0134b57f3ee2192ca2c0bbe3ee241223542a89006f9e8c2cfabfaa ] || { log "STOP: capture hash $h"; exit 1; }
log "capture verified: big8m.pcap $(stat -c%s big8m.pcap) bytes, same SHA-256 as the cloud test"
if [ ! -f plain/conn.log ]; then
  mkdir -p plain; cd plain; s=$(date +%s.%N); $ZEEK -C -r ../big8m.pcap local "$ZK/tools/notimerthrottle.zeek" >/dev/null 2>&1; log "plain zeek: $(el $s) s"; cd "$D"
fi
for k in $KS; do
  W="$D/k$k"; mkdir -p "$W/shards"; s=$(date +%s.%N)
  m=$("$ZK/tools/hs_pcapsplit" big8m.pcap $k "$W/shards" --ticks); t1=$(date +%s.%N)
  for i in $(seq 0 $((k-1))); do f=$(printf "%02d" $i); mkdir -p "$W/s$f"; (cd "$W/s$f" && $ZEEK -C -r "$W/shards/shard$f.pcap" local "$ZK/tools/hs_shard.zeek" "$ZK/tools/notimerthrottle.zeek" >/dev/null 2>&1) & done; wait; t2=$(date +%s.%N)
  python3 -I "$ZK/tools/hs_zeek_merge.py" "$W/merged" "$W"/s[0-9]*; t3=$(date +%s.%N)
  log "$k shards: total $(el $s) s (split $(python3 -c "print(f'{$t1-$s:.2f}')"), zeek $(python3 -c "print(f'{$t2-$t1:.2f}')"), merge $(el $t2)) [$m]"
  python3 -I "$ZK/tools/zeekcmp.py" plain "$W/merged" | grep -v IDENTICAL | sed 's/^/    /' | tee -a "$L"
  rm -rf "$W/shards"
done
log "== finished $(date)"; log "DONE"
