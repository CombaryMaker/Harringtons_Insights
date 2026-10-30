#!/bin/bash
source files.sh
f="$1"; name=$(echo "$f" | tr -d ' -' | tr '=' '_')
clang++ -O2 -ffp-contract=off -DBBITS=16 -DCUMHASH=1 $f -o /tmp/sw_$name engine9.cpp 2>&1 | grep error
echo "$f : $(/tmp/sw_$name $FILES | tail -1 | sed -E 's/.*per-million ([0-9]+).*all-ok ([0-9]).*/\1 ok \2/')"
