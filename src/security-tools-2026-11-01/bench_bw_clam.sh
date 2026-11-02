# bench_bw_clam.sh - binwalk and ClamAV, plain vs split pieces, on fw.img (2-core cloud machine). MIT License.
SEC=$1; I=$SEC/img/fw.img; R=$SEC/results; mkdir -p $R; cd $SEC/img
BW=$SEC/prefix/bin/binwalk; CS=$SEC/clam/usr/local/bin/clamscan
export LD_LIBRARY_PATH=$SEC/clam/usr/local/lib CVD_CERTS_DIR=$SEC/clam/usr/local/etc/certs
t() { local s=$(date +%s.%N); "$@"; echo "$(echo "$(date +%s.%N) - $s" | bc)"; }
echo "fw.img $(stat -c%s $I) bytes, sha256 $(sha256sum $I | cut -c1-16), $(nproc) cores"
echo "== binwalk 3.1.0"
s=$(date +%s.%N); $BW -q -l $R/bw_plain.json $I; echo "plain binwalk (default threads): $(echo "$(date +%s.%N)-$s"|bc) s"
s=$(date +%s.%N); $BW -q -t 1 -l $R/bw_plain_t1.json $I; echo "plain binwalk -t 1: $(echo "$(date +%s.%N)-$s"|bc) s"
for k in 2 4 8; do s=$(date +%s.%N); m=$(python3 -I $SEC/tools/hs_binwalk.py $BW $I $k 4 $R/bw_split$k.json); echo "split $k pieces, 4 MB overlap: $(echo "$(date +%s.%N)-$s"|bc) s ($m) | $(python3 -I $SEC/tools/bwcmp.py $R/bw_plain.json $R/bw_split$k.json | head -1)"; done
echo "== ClamAV 1.5.1, $(ls $SEC/clamdb | wc -l) rule files (Neo23x0 signature-base YARA, loadable part) + EICAR"
s=$(date +%s.%N); $CS --no-summary --allmatch -d $SEC/clamdb $I 2>/dev/null > $R/clam_default.txt; echo "plain clamscan, default limits: $(echo "$(date +%s.%N)-$s"|bc) s: $(grep -c FOUND $R/clam_default.txt) found; $(cat $R/clam_default.txt | head -2 | cut -c1-150)"
s=$(date +%s.%N); $CS --no-summary --allmatch --max-filesize=4000M --max-scansize=4000M -d $SEC/clamdb $I 2>/dev/null | grep FOUND | sed 's/.*: //;s/ FOUND$//' | sort -u > $R/clam_plain.txt; echo "plain clamscan, limits raised to scan all: $(echo "$(date +%s.%N)-$s"|bc) s: $(wc -l < $R/clam_plain.txt) names"
for k in 2 4 8 16; do s=$(date +%s.%N); python3 -I $SEC/tools/hs_clam.py $CS $SEC/clamdb $I $k 1024 2>/dev/null > $R/clam_split$k.txt; echo "split $k pieces, 1 MB overlap: $(echo "$(date +%s.%N)-$s"|bc) s: $(wc -l < $R/clam_split$k.txt) names, same as plain: $(cmp -s $R/clam_plain.txt $R/clam_split$k.txt && echo yes || echo NO)"; done
