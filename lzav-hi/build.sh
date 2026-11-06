#!/bin/sh
# build.sh - builds lzav-hi tools on Linux (gcc/g++). Michael Hugh Harrington, 2026, MIT License.
#
#   ./build.sh            build bench, lzavhi CLI and hs_lzav in ./bin
#   ./build.sh verify     also clone UPSTREAM LZAV into ./upstream-lzav, show that lzav.h here is byte-identical
#                         to it, and build a decoder (dec_upstream) against the upstream copy only
set -e
cd "$(dirname "$0")"
mkdir -p bin
LZ4_TAG=v1.10.0
if [ ! -d lz4 ]; then git clone -q --depth 1 --branch $LZ4_TAG https://github.com/lz4/lz4.git lz4; fi
CFLAGS="-O3 -Wall -Wno-unused-function -Wno-unused-result"
gcc $CFLAGS -I. -Ilz4/lib bench.c lz4/lib/lz4.c lz4/lib/lz4hc.c -o bin/bench
gcc $CFLAGS -I. lzavhi_cli.c -o bin/lzavhi
g++ -O2 -std=c++17 -pthread -Wno-unused-function -Wno-misleading-indentation -I. hs_lzav.cpp -o bin/hs_lzav
echo "built: bin/bench bin/lzavhi bin/hs_lzav"

if [ "$1" = "verify" ]; then
  rm -rf upstream-lzav
  git clone -q https://github.com/avaneev/lzav.git upstream-lzav
  echo "upstream commit: $(git -C upstream-lzav log -1 --format='%H %ad')"
  sha256sum lzav.h upstream-lzav/lzav.h
  if cmp -s lzav.h upstream-lzav/lzav.h; then echo "lzav.h here is IDENTICAL to upstream"; else
     echo "NOTE: upstream lzav.h differs from the copy here (upstream moved on); dec_upstream uses upstream's"; fi
  # A decoder that includes ONLY upstream's lzav.h (no lzav_hi.h at all).
  cat > bin/dec_upstream.c <<'EOF'
#include "lzav.h"
#include <stdio.h>
#include <stdlib.h>
int main(int c, char **v) {
  if (c < 3) { fprintf(stderr, "usage: dec_upstream IN.lzavhi OUT\n"); return 2; }
  FILE *f = fopen(v[1], "rb"); if (!f) return 1; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *in = malloc(n); if (fread(in, 1, n, f) != (size_t)n) return 1; fclose(f);
  unsigned long long ol = 0; for (int i = 0; i < 8; i++) ol |= (unsigned long long)in[i] << (8 * i);
  unsigned char *out = malloc(ol + 1);
  int r = lzav_decompress(in + 8, out, (int)(n - 8), (int)ol);
  if (r != (int)ol) { fprintf(stderr, "decode error %d\n", r); return 1; }
  f = fopen(v[2], "wb"); fwrite(out, 1, ol, f); fclose(f); printf("upstream lzav_decompress: %d bytes OK\n", r); return 0; }
EOF
  gcc -O2 -Iupstream-lzav bin/dec_upstream.c -o bin/dec_upstream
  echo "built: bin/dec_upstream (upstream lzav.h only)"
fi
