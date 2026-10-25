#!/usr/bin/env python3
"""bzip2-hi: smaller standard .bz2 files (Harrington Insights).

Runs the tuned bzip2 encoder (built from ./source) at every block size
(-1 .. -9) and keeps the smallest result. The output is a normal .bz2 file:
any bzip2 decodes it, and in our tests it decodes as fast or faster than
bzip2 -9 output.

usage: bz2_hi.py INPUT [OUTPUT]          (default OUTPUT = INPUT.bz2)
       set BZIP2_HI to the tuned binary if it is not ./source/bzip2
"""
import os, subprocess, sys

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src + '.bz2'
    exe = os.environ.get('BZIP2_HI', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'source', 'bzip2'))
    best = None
    for level in range(9, 0, -1):
        blob = subprocess.run([exe, '-%d' % level, '-c', src], capture_output=True, check=True).stdout
        if best is None or len(blob) < len(best[1]):
            best = (level, blob)
    with open(dst, 'wb') as f:
        f.write(best[1])
    print('%s -> %s  %d bytes (block size -%d)' % (src, dst, len(best[1]), best[0]))
    return 0

if __name__ == '__main__':
    sys.exit(main())
