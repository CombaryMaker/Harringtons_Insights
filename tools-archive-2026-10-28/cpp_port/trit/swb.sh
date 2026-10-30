#!/bin/bash
# binaries only: usage swb.sh "-DFLAGS"
cd /tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cpp_port/trit
SRC=${SRC:-bin_bench.cpp}
name=b$(echo "$1" | tr -d ' -' | tr '=' '_')
clang++ -O2 -ffp-contract=off -Wno-unused-function -Wno-unused-label -Wno-unused-variable $1 -o bin/$name $SRC 2>&1 | grep error
bin/$name $(cat binfiles.txt) | awk -v f="$1" '{if($1=="TOTAL") t=$0; else s=s sprintf("%s %s %s  ", $1, $4, $6)} END {print f": " s " | " t}'
