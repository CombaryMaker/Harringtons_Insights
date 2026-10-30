#!/usr/bin/env python3
"""run1.py CMD... -> runs one command, prints 'peakKB cpu_s wall_s' (peak memory of that command alone)."""
import resource, subprocess, sys, time
t0 = time.perf_counter(); r = subprocess.run(sys.argv[1:], capture_output=True); w = time.perf_counter() - t0
u = resource.getrusage(resource.RUSAGE_CHILDREN)
if r.returncode: sys.stderr.write(r.stderr.decode()); sys.exit(r.returncode)
print(u.ru_maxrss, u.ru_utime + u.ru_stime, w)
