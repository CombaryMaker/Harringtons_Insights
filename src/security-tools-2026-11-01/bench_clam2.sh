# bench_clam2.sh - ClamAV plain vs split pieces on fw.img, scan-time limit off (2-core cloud machine). MIT License.
SEC=$1; I=$SEC/img/fw.img; R=$SEC/results; CS=$SEC/clam/usr/local/bin/clamscan
export LD_LIBRARY_PATH=$SEC/clam/usr/local/lib CVD_CERTS_DIR=$SEC/clam/usr/local/etc/certs
s=$(date +%s.%N); $CS --no-summary --allmatch --max-filesize=2000M --max-scansize=2000M --max-scantime=0 -d $SEC/clamdb $I 2>/dev/null | grep FOUND | sed 's/.*: //;s/ FOUND$//' | sort -u > $R/clam_plain.txt; echo "plain clamscan, all limits off: $(echo "$(date +%s.%N)-$s"|bc) s: $(wc -l < $R/clam_plain.txt) names"
for k in 2 4 8 16; do s=$(date +%s.%N); python3 -I $SEC/tools/hs_clam.py $CS $SEC/clamdb $I $k 1024 2>/dev/null > $R/clam_split$k.txt; echo "split $k pieces, 1 MB overlap: $(echo "$(date +%s.%N)-$s"|bc) s: $(wc -l < $R/clam_split$k.txt) names, same as plain: $(cmp -s $R/clam_plain.txt $R/clam_split$k.txt && echo yes || echo NO)"; done
