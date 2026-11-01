#!/usr/bin/env bash
# Builds hs_zstd (1.3-ablation) with zstd 1.5.6 statically linked. Linux, WSL2, macOS, or MSYS2/MinGW-w64 on Windows.
set -euo pipefail
cd "$(dirname "$0")"
if [ ! -f zstd-1.5.6/lib/libzstd.a ]; then
  [ -f zstd-1.5.6.tar.gz ] || curl -L -o zstd-1.5.6.tar.gz https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
  tar xzf zstd-1.5.6.tar.gz
  make -C zstd-1.5.6/lib -j4 libzstd.a
fi
g++ -O2 -std=c++17 -pthread hs_zstd.cpp -Izstd-1.5.6/lib zstd-1.5.6/lib/libzstd.a -o hs_zstd
echo "built ./hs_zstd"; ./hs_zstd 2>&1 | head -3 || true
