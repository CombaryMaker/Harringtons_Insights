#!/usr/bin/env python3
"""lpaq1-h best-of driver: compresses a file with each flag setting and keeps the smallest.
usage: lpaq1h_best.py LPAQ1H_BINARY N INPUT OUTPUT   (N = lpaq1 memory option 0..9)
Flags 0 give a stock lpaq1 file; any other choice needs lpaq1-h to decode (lpaq1h d IN OUT)."""
import os, subprocess, sys, shutil
exe, n, src, dst = sys.argv[1:5]
best = None
for fl in (0, 4, 9, 13, 11, 15):   # plain, match recovery, record ctx (word slot), +recovery, record ctx (word+order6), +recovery
    tmp = dst + '.tmp'
    subprocess.run([exe, n, src, tmp], check=True, capture_output=True, env=dict(os.environ, LPH_FLAGS=str(fl)))
    size = os.path.getsize(tmp)
    if best is None or size < best[0]:
        best = (size, fl); shutil.move(tmp, dst)
    else:
        os.remove(tmp)
print(f'{src}: {os.path.getsize(src)} -> {best[0]} bytes (flags {best[1]})')
