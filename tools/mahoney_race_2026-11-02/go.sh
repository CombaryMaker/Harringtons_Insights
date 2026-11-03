#!/bin/bash
# go.sh - Matt Mahoney test sets race: Calgary, Canterbury large, Generic Compression Benchmark (uiq2), enwik9.
# Runs inside the ZeekTest WSL Linux, started by race.cmd / race.ps1 from this folder.
# Michael Hugh Harrington, 2026. MIT License (this script; ZPAQ is public domain/MIT, lpaq1 and uiq2 are GPL).
# Every result: compressed bytes, compress time, decode time, peak memory, exact round-trip check.
W=$(pwd); R="$W/results.csv"; P="$W/progress.txt"; S="$W/sysinfo.txt"
log(){ echo "$(date '+%F %T') $*" >> "$P"; }
stop(){ log "STOP: $*"; echo DONE >> "$P"; exit 1; }
: > "$P"; log "start in $W"
WK=/root/mh; rm -rf "$WK"; mkdir -p "$WK/bin" "$WK/data" "$WK/out" || stop "cannot make work folder"

# 1. verify everything before running (bundle hashes, then every file inside)
sha256sum -c BUNDLES.sha256 --quiet || stop "bundle hash mismatch in Windows folder"
tar -xJf bin.tar.xz -C "$WK/bin" && (cd "$WK/bin" && sha256sum -c SHA256SUMS --quiet) || stop "tool check failed"
tar -xJf data.tar.xz -C "$WK/data" && (cd "$WK/data" && sha256sum -c SHA256SUMS --quiet) || stop "data check failed"
cat enwik9.zip.part* > "$WK/enwik9.zip" && (cd "$WK" && echo "99cdb5ac84392252d3f0912ccedd195bc95bd80cbef3b0cdf2eee4ad9a3b7a51  enwik9.zip" | sha256sum -c --quiet) \
  || stop "enwik9.zip hash mismatch"
mkdir -p "$WK/data/enwik9" && python3 -m zipfile -e "$WK/enwik9.zip" "$WK/data/enwik9/" && rm -f "$WK/enwik9.zip"
(cd "$WK/data/enwik9" && echo "e206c3450ac99950df65bf70ef61a12d  enwik9" | md5sum -c --quiet) || stop "enwik9 md5 mismatch"
log "all tools and data verified (sha256 of $(wc -l < "$WK/bin/SHA256SUMS") tool files, $(wc -l < "$WK/data/SHA256SUMS") data files, enwik9 md5 e206c345...)"
for t in python3 cmp sha256sum md5sum; do command -v $t >/dev/null || stop "$t missing"; done

# 2. machine info and ZPAQ thread count (ZPAQ -m5 needs about 1 GB per thread; WSL has its own memory limit)
MEMMB=$(awk '/MemTotal/{print int($2/1024)}' /proc/meminfo); NP=$(nproc)
ZT=$(( (MEMMB - 1536) / 1000 )); [ $ZT -gt $NP ] && ZT=$NP; [ $ZT -lt 1 ] && ZT=1
{ echo "date: $(date)"; echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-)"; echo "threads (nproc): $NP"
  echo "WSL memory MB: $MEMMB"; echo "ZPAQ threads used: $ZT"; echo "kernel: $(uname -r)"; } > "$S"
log "cpu threads $NP, WSL memory ${MEMMB} MB, ZPAQ threads $ZT"

B="$WK/bin"; RT="$B/runt"
echo "set,file,tool,orig_bytes,comp_bytes,c_wall_s,c_time_s,d_wall_s,c_mem_kb,d_mem_kb,exact" > "$R"

run() {   # run SET FILE TOOL
  local set=$1 f=$2 tool=$3 D="$WK/data/$1" O="$WK/out/w"
  rm -rf "$O"; mkdir -p "$O/x"; cd "$D" || return
  local cmp_out="$O/c.out" arc dec dcmd
  case $tool in
    zpaq_m4|zpaq_m5|zpaqhi_m5)
      local exe=zpaq m=${tool##*_}; [ $tool = zpaqhi_m5 ] && exe=zpaq-hi
      arc="$O/a.zpaq"; RUNT_OUT="$cmp_out" "$RT" "$O/c.t" "$B/$exe" a "$arc" "$f" -$m -threads $ZT
      dec="$O/x/$f"; dcmd=("$B/zpaq" x "$arc" -to "$O/x" -threads $ZT) ;;
    lpaq1_9)
      arc="$O/a.lp"; "$RT" "$O/c.t" "$B/lpaq1" 9 "$f" "$arc"; dec="$O/x/out"; dcmd=("$B/lpaq1" d "$arc" "$dec") ;;
    lpaq1h_9)
      arc="$O/a.lp"; "$RT" "$O/c.t" python3 "$B/lpaq1h_best.py" "$B/lpaq1h" 9 "$f" "$arc"; dec="$O/x/out"; dcmd=("$B/lpaq1h" d "$arc" "$dec") ;;
    hcm_fast|hcm_max|hcm_sets_fast|hcm_sets_max|hcmx_max)
      local exe=hcm c; case $tool in hcm_fast) c=c;; hcm_max) c=x;; hcm_sets_fast) c=s;; hcm_sets_max) c=t;; hcmx_max) exe=hcmx; c=x;; esac
      arc="$O/a.hcm"; RUNT_OUT="$cmp_out" "$RT" "$O/c.t" "$B/$exe" $c "$f" "$arc"; dec="$O/x/out"; dcmd=("$B/$exe" d "$arc" "$dec") ;;
  esac
  local csz=$(stat -c %s "$arc" 2>/dev/null || echo 0)
  "$RT" "$O/d.t" "${dcmd[@]}"
  local ok=no; cmp -s "$f" "$dec" && ok=yes
  read cw ccpu cmem crc < "$O/c.t"; read dw dcpu dmem drc < "$O/d.t"
  local ct=$cw
  if [[ $tool == hcm* ]]; then   # HCM checks its own output after compressing; take that check decode out of compress time
    local chk=$(grep -o '[0-9.]* s to check' "$cmp_out" | awk '{print $1}'); [ -n "$chk" ] && ct=$(awk -v a=$cw -v b=$chk 'BEGIN{printf "%.3f", a-b}')
  fi
  echo "$set,$f,$tool,$(stat -c %s "$f"),$csz,$cw,$ct,$dw,$cmem,$dmem,$ok" >> "$R"
  [ $ok = yes ] || log "NOT EXACT: $set $f $tool (exit $crc/$drc)"
  rm -rf "$O"
}

SMALL=${SMALL_TOOLS:-"zpaq_m4 zpaq_m5 lpaq1_9 hcm_fast hcm_max hcm_sets_fast hcm_sets_max hcmx_max lpaq1h_9 zpaqhi_m5"}
for set in ${SMALL_SETS:-calgary large gcb}; do
  for tool in $SMALL; do
    for f in $(cd "$WK/data/$set" && ls | head -n ${MAX_FILES:-1000}); do run $set "$f" $tool; done
    log "$set $tool done"
  done
done
python3 "$B/summary.py" "$R" > "$W/results_summary.txt"; log "small sets finished - summary written"

for tool in ${E9_TOOLS-zpaq_m4 zpaq_m5 hcm_sets_fast hcm_sets_max lpaq1_9 zpaqhi_m5 hcm_max}; do
  log "enwik9 $tool starting"; run enwik9 enwik9 $tool; log "enwik9 $tool done: $(tail -1 "$R")"
  python3 "$B/summary.py" "$R" > "$W/results_summary.txt"
done
cd "$W"; rm -rf "$WK"; log "work folder inside Linux removed"
echo DONE >> "$P"
