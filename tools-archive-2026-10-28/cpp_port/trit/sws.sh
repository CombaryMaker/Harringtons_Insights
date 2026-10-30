#!/bin/bash
cd /tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cpp_port/trit
name=s$(echo "$1" | tr -d ' -' | tr '=' '_')
clang++ -O2 -ffp-contract=off -Wno-unused-function -Wno-unused-label -Wno-unused-variable $1 -o bin/$name bin_bench.cpp 2>&1 | grep error
echo "$1: $(bin/$name $(cat sumfile.txt) | head -1 | awk '{print $4}')"
