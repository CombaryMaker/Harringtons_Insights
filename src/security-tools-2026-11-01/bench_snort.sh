# bench_snort.sh - Snort 3.12.4 + Snort 3 community rules: plain vs Harrington Sets host-pair shards run by Snort's own
# packet threads (--pcap-dir, -z N). -H (deterministic hash tables) on every run. 2-core cloud. MIT License.
SEC=$1; S=$SEC/snort/bin/snort; C=$SEC/snort/etc/snort/snort.lua; X="$(cat $SEC/rules/hs_extra.lua)"; P=$SEC/pc/big8m.pcap; W=$SEC/w/snb
el() { echo "$(date +%s.%N) - $1" | bc; }
mkdir -p $W/plain; s=$(date +%s.%N); $S -q -H -c $C --lua "$X" -r $P -A csv -l $W/plain >/dev/null 2>&1; echo "plain snort: $(el $s) s"
sort $W/plain/alert_csv.txt > $W/plain.txt
for k in 2 4; do
  D=$W/split$k; mkdir -p $D/shards $D/out; s=$(date +%s.%N)
  $SEC/tools/hs_pcapsplit $P $k $D/shards >/dev/null; t1=$(date +%s.%N)
  $S -q -H -c $C --lua "$X" --pcap-dir $D/shards -z 2 -A csv -l $D/out >/dev/null 2>&1; t2=$(date +%s.%N)
  cat $D/out/*alert_csv.txt | sort > $D/split.txt; rm -r "$D/shards"
  echo "split $k shards, -z 2: $(el $s) s total (split $(echo "$t1-$s"|bc) s, snort $(echo "$t2-$t1"|bc) s)"
  python3 -I - $W/plain.txt $D/split.txt <<'PY'
import sys, collections
a = [l.rstrip('\n') for l in open(sys.argv[1])]; b = [l.rstrip('\n') for l in open(sys.argv[2])]
ps = lambda L: [l for l in L if ', 122, ' in l]; np = lambda L: [l for l in L if ', 122, ' not in l]
A, B = collections.Counter(np(a)), collections.Counter(np(b)); At = collections.Counter(l.split(', ', 1)[1] for l in np(a)); Bt = collections.Counter(l.split(', ', 1)[1] for l in np(b))
print(f'  alerts plain {len(a)}, split {len(b)}; non-port-scan identical incl. time: {sum((A & B).values())} of {len(np(a))}; '
      f'identical ignoring time: {At == Bt}; port_scan plain {len(ps(a))}, split {len(ps(b))}')
PY
done
