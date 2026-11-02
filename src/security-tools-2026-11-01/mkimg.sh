# mkimg.sh - firmware-style test image from public data only. MIT License.
# Pieces, each aligned to 64 KB with 0xFF padding (like flash): Silesia corpus tar; gzip/xz/bzip2/zstd copies of
# Silesia members; a tar of the Ubuntu system shared libraries (public open-source binaries); the public pcap set;
# three EICAR test strings at fixed offsets (standard antivirus test string, harmless).
set -e; S=$1; SEC=$S/sec; O=$2; T=$(mktemp -d -p $SEC); : > $O
pad() { n=$(stat -c%s $O); r=$(( (65536 - n % 65536) % 65536 )); head -c $r /dev/zero | tr '\0' '\377' >> $O; }
add() { cat "$1" >> $O; pad; }
E='X5O!P%@AP[4\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*'
tar -C $S/tb -xf $S/tb/silesia.tar -C $T 2>/dev/null || (mkdir -p $T && tar -xf $S/tb/silesia.tar -C $T)
add $S/tb/silesia.tar
printf '%s' "$E" >> $O; pad
for f in dickens mozilla; do gzip -6 -c $T/$f > $T/$f.gz; add $T/$f.gz; done
for f in samba webster; do xz -6 -c $T/$f > $T/$f.xz; add $T/$f.xz; done
bzip2 -9 -c $T/nci > $T/nci.bz2; add $T/nci.bz2
$S/tools/zstd -q -19 -c $T/ooffice > $T/oo.zst; add $T/oo.zst
printf '%s' "$E" >> $O; pad
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=2026-10-01 -cf $T/libs.tar -C /usr/lib/x86_64-linux-gnu $(cd /usr/lib/x86_64-linux-gnu && ls | grep '\.so' | head -400) 2>/dev/null || true; add $T/libs.tar
add $SEC/pc/base.pcap
printf '%s' "$E" >> $O; pad
rm -rf $T
