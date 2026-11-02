#!/bin/bash
# go.sh - runs inside the ZeekTest WSL Linux, started by start.ps1 from this folder. MIT License.
W=$(pwd); R="$W/results.txt"
mkdir -p /opt && rm -rf /opt/zk && tar -xJf zk.tar.xz -C /opt && (cd /opt/zk && sha256sum -c SHA256SUMS --quiet) \
  && echo "bundle files verified inside Linux ($(wc -l < /opt/zk/SHA256SUMS) files)" >> "$R" || { echo "STOP: bundle check failed" >> "$R"; echo DONE >> "$R"; exit 1; }
command -v python3 >/dev/null || { echo "STOP: python3 missing in Ubuntu image" >> "$R"; echo DONE >> "$R"; exit 1; }
RESULTS="$R" bash /opt/zk/run_pc.sh /root/zt "2 4 6 8 12"
