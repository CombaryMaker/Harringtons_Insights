#!/bin/bash
# Books: grouping schemes and the paragraph guessers. Needs data/ (get-data.sh) and bin/ (build.sh).
# Optional: set LPAQ1 (stock lpaq1), KANZI (kanzi 2.6.0), BSC (bsc 3.4.0) to also run those tools.
for f in data/pg*.body; do
  python3 src/reorder2.py $f                       # writes .g2 .g3 .g4 .gc and prints the note cost of each
  for v in "" .g2 .g3 .gc; do x=$f$v
    echo "$x model-C   $(bin/para C  $x | awk '{print $(NF-1)}')"
    echo "$x model-AC  $(bin/para AC $x | awk '{print $(NF-1)}')"
    echo "$x xz        $(xz -9e -c $x | wc -c)"
    [ -n "$LPAQ1" ] && { $LPAQ1 9 $x /tmp/o.lp >/dev/null; echo "$x lpaq1     $(stat -c%s /tmp/o.lp)"; }
    [ -n "$KANZI" ] && { $KANZI -c -i $x -o /tmp/o.knz -l 9 -f >/dev/null; echo "$x kanzi-l9  $(stat -c%s /tmp/o.knz)"; }
    [ -n "$BSC" ]   && { $BSC e $x /tmp/o.bsc -b64e2 >/dev/null; echo "$x bsc       $(stat -c%s /tmp/o.bsc)"; }
  done
  for b in lpaq1-para lpaq1-wordpair lpaq1-wp-para lpaq1-wp-sparse; do bin/$b 9 $f /tmp/o.$b >/dev/null; echo "$f $b $(stat -c%s /tmp/o.$b)"; done
  bin/lpaq1-wp-para 9 $f.g2 /tmp/o.g2 >/dev/null; echo "$f.g2 lpaq1-wp-para $(stat -c%s /tmp/o.g2)"
done
# Grouped sizes must have the printed note cost added before comparing with the original.
