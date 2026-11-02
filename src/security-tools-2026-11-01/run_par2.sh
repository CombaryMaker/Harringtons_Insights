#!/bin/bash
# run_par2.sh - Zeek with Harrington's Sets on the desktop, round 2 (runs inside the ZeekTest WSL Linux). MIT License.
# Michael Hugh Harrington, 2026.
# Changes from round 1: the 7.2 GB capture is rebuilt with realistic dates (64 copies 10,000,000 s apart, last copy in
# 2027 - round 1 used 40,000,000 s and reached 2088, where Zeek re-logs known hosts every few seconds); the merge
# compares times as whole microseconds (faster, same output); a one-pass runner (hs_zeek_par1) that reads the capture
# once is tested beside the plan-first runner (hs_zeek_par). Online lookups off and max_timer_expires = 0 everywhere.
W=$(pwd); R="$W/results_par2.txt"; ZK=/opt/zk; . $ZK/env.sh
log() { echo "$*" | tee -a "$R"; }
el() { python3 -c "import sys;print(f'{float(sys.argv[2])-float(sys.argv[1]):.2f}')" "$1" "$(date +%s.%N)"; }
cd "$W/par2" && sha256sum -c SHA256SUMS --quiet || { log "STOP: file check failed"; log DONE; exit 1; }
cp hs_zeek_par1 $ZK/tools/ && cp hs_zeek_merge_par.py $ZK/tools/ && chmod +x $ZK/tools/hs_zeek_par1
log "== round 2, files verified, $(date)"
log "machine: $(nproc) CPUs visible ($(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')), RAM $(free -g | awk '/Mem/{print $2}') GB"
rm -f /root/big64.pcap; B=/root/big64r.pcap
[ -f $B ] || $ZK/tools/bigcapn $ZK/base_m.pcap $B 64 10000000 2>/dev/null
h=$(sha256sum $B | cut -c1-64); [ "$h" = 93a45045d265854fc82b9947fc794663fe88d985ea8a96dfac7286a262a9f28e ] || { log "STOP: capture hash $h"; log DONE; exit 1; }
log "capture verified: big64r.pcap $(stat -Lc%s $B) bytes, 7,718,336 packets (SHA-256 matches the cloud)"
OFF="$ZK/tools/notimerthrottle.zeek $ZK/tools/offline.zeek"; SH="$ZK/tools/hs_shard.zeek"
T=/root/par2; rm -rf "${T:?}"; mkdir -p $T/plain; cd $T/plain
s=$(date +%s.%N); $ZEEK -C -r $B local $OFF >/dev/null 2>&1; log "plain zeek: $(el $s) s"
run() {   # $1 = runner, $2 = pieces
  D=$T/$1_k$2; s=$(date +%s.%N)
  out=$($ZK/tools/$1 $B $2 $D/run 10000 -- $ZEEK local $SH $OFF); t1=$(date +%s.%N)
  python3 -I $ZK/tools/hs_zeek_merge_par.py $D/merged $D/run/s[0-9]*
  log "$1, $2 pieces: total $(el $s) s (split+zeek $(python3 -c "print(f'{$t1-$s:.2f}')"), merge $(el $t1))"
  echo "$out" | sed 's/^/    /' >> "$R"
  python3 -I $ZK/tools/zeekcmp.py $T/plain $D/merged | grep -v IDENTICAL | sed 's/^/    /' | tee -a "$R"
  rm -rf "${D:?}/run"
}
for k in 4 6 8 12; do run hs_zeek_par1 $k; done
for k in 6 12; do run hs_zeek_par $k; done
log "== finished $(date)"; log DONE
