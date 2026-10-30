#!/bin/bash
# q.sh EXE MODEBITS(text) MODEBITS(bin) -> per-file sizes and total (Canterbury), round trip checked
X=$1; D=~/dl/cant/canterbury; W=$(mktemp -d); T=0
for f in alice29.txt asyoulik.txt cp.html fields.c grammar.lsp kennedy.xls lcet10.txt plrabn12.txt ptt5 sum xargs.1; do
 case $f in kennedy.xls|ptt5|sum) m=$3;; *) m=$2;; esac
 HCMX_ONLY=$m $X c $D/$f $W/c >/dev/null 2>&1; $X d $W/c $W/d >/dev/null 2>&1
 s=$(stat -c%s $W/c); cmp -s $D/$f $W/d || echo BAD $f; T=$((T+s)); printf "%s %s  " $f $s
done; echo; echo TOTAL $T; rm -rf $W
