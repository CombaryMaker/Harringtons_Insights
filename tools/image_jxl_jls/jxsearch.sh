#!/bin/bash
# usage: jxsearch.sh image tag args...   -> prints: tag size mps
J=~/sil/libjxl/b/tools; f=$1; tag=$2; shift 2
o=~/imgbench/jx3/$(basename $f .pgm).$tag.jxl
$J/cjxl $f $o -d 0 --num_threads=0 --quiet "$@" 2>/dev/null || { echo "$tag FAIL"; exit; }
m=$($J/djxl $o --num_threads=0 --num_reps=3 --disable_output 2>&1 | grep -o 'geomean: [0-9.]*' | cut -d' ' -f2)
echo "$tag $(stat -c%s $o) $m"
