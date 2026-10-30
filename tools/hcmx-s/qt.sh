#!/bin/bash
# qt.sh EXE MODE_TEXT MODE_BIN [DIR] -> per-file sizes, total size, total decode seconds (exact round trip checked)
X=$1; D=${4:-$HOME/dl/cant/canterbury}; W=$(mktemp -d); T=0; DT=0
for p in $D/*; do f=$(basename $p); [ -f "$p" ] || continue; case $f in SHA1SUM|*.zip|*.md) continue;; esac
 case $f in kennedy.xls|ptt5|sum|mozilla|mr|ooffice|osdb|sao|x-ray) m=$3;; *) m=$2;; esac
 HCMX_ONLY=$m $X c $p $W/c >/dev/null 2>&1
 s0=$(date +%s.%N); $X d $W/c $W/d >/dev/null 2>&1; s1=$(date +%s.%N)
 s=$(stat -c%s $W/c); cmp -s $p $W/d || echo BAD $f; T=$((T+s)); DT=$(echo "$DT + $s1 - $s0" | bc); printf "%s %s %.2f\n" $f $s $(echo "$s1 - $s0" | bc)
done; echo TOTAL $T DEC $DT; rm -rf $W
