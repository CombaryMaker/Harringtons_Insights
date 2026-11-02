#!/bin/bash
# offline.sh - 1) second plain run to see whether plain Zeek repeats itself with internet on;
#              2) full test again with Zeek's online malware-hash lookups (Team Cymru, detect-MHR) turned off,
#                 since their timestamps depend on internet response time. MIT License.
W=$(pwd); R="$W/results_offline.txt"; ZK=/opt/zk; . $ZK/env.sh
log() { echo "$*" | tee -a "$R"; }
cat > $ZK/tools/offline.zeek <<'Z'
# offline.zeek - no online lookups: detect-MHR (Team Cymru Malware Hash Registry) checks no file types.
@load frameworks/files/detect-MHR
redef TeamCymruMalwareHashRegistry::match_file_types = /^NO-LOOKUPS$/;
Z
log "== second plain run (internet on, same settings as the first), $(date)"
mkdir -p /root/zt/plain_again && cd /root/zt/plain_again && $ZEEK -C -r ../big8m.pcap local $ZK/tools/notimerthrottle.zeek >/dev/null 2>&1
log "plain run 1 vs plain run 2: $(python3 -I $ZK/tools/zeekcmp.py /root/zt/plain /root/zt/plain_again | grep -v IDENTICAL | tr '\n' ' ')"
sed 's|"$ZK/tools/notimerthrottle.zeek"|"$ZK/tools/notimerthrottle.zeek" "$ZK/tools/offline.zeek"|g' $ZK/run_pc.sh > $ZK/run_pc_offline.sh
mkdir -p /root/zt2 && ln -sf /root/zt/big8m.pcap /root/zt2/big8m.pcap
log "== full test with online lookups off (plain and all shard runs load offline.zeek)"
cd "$W"; RESULTS="$R" bash $ZK/run_pc_offline.sh /root/zt2 "2 4 6 8 12"
