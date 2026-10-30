#!/usr/bin/env python3
"""tm.py FILE REPS exe:mode:label ... -> size, min decode CPU seconds over REPS runs (exact check)."""
import sys, os, subprocess, resource, filecmp, tempfile
f, reps = sys.argv[1], int(sys.argv[2]); W = tempfile.mkdtemp()
for spec in sys.argv[3:]:
    exe, mode, label = spec.split(':')
    subprocess.run([exe, 'c', f, W + '/c'], env=dict(os.environ, HCMX_ONLY=mode), check=True, capture_output=True)
    ts = []
    for _ in range(reps):
        r0 = resource.getrusage(resource.RUSAGE_CHILDREN)
        subprocess.run([exe, 'd', W + '/c', W + '/d'], check=True, capture_output=True)
        r1 = resource.getrusage(resource.RUSAGE_CHILDREN)
        ts.append(r1.ru_utime - r0.ru_utime + r1.ru_stime - r0.ru_stime)
    print('%-10s %-14s %10d %7.2f s %s' % (os.path.basename(f), label, os.path.getsize(W + '/c'), min(ts),
          'OK' if filecmp.cmp(f, W + '/d', shallow=False) else 'BAD'), flush=True)
