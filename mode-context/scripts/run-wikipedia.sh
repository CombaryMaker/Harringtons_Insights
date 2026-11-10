#!/bin/bash
# Wikipedia: segment-type context inside lpaq1, and line grouping. Needs data/s0 s40 s80 and bin/.
for s in data/s0 data/s40 data/s80; do
  for b in lpaq1-wiki-control lpaq1-wiki; do
    bin/$b 9 $s /tmp/w.$b >/dev/null; bin/$b d /tmp/w.$b /tmp/w.out >/dev/null
    cmp -s /tmp/w.out $s && ok=exact || ok=FAILED; echo "$s $b $(stat -c%s /tmp/w.$b) decode $ok"
  done
  python3 src/wgroup.py $s                          # writes .wg (grouped) and .wc (control), prints the note cost
  for v in "" .wg .wc; do echo "$s$v xz $(xz -9e -c $s$v | wc -c)"; bin/lpaq1-wiki 9 $s$v /tmp/w.g >/dev/null; echo "$s$v lpaq1-wiki $(stat -c%s /tmp/w.g)"; done
done
