# hs_clam.py - scan one large file with ClamAV as overlapping pieces in parallel. Michael Hugh Harrington, 2026.
# MIT License. Piece i covers [start_i, end_i + overlap); the overlap is longer than any loaded signature, so a match
# that crosses a cut is still seen whole by one piece. Result: the set of detection names (ClamAV --allmatch), which is
# what a scan verdict reports; plain clamscan gives no match offsets to compare beyond that.
# Usage: hs_clam.py CLAMSCAN DBDIR FILE PIECES OVERLAP_KB
import os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
cs, db, src, k, ov = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]) << 10
n = os.path.getsize(src); step = -(-n // k); tmp = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(src)))
for i in range(k):
    with open(src, 'rb') as a, open(f'{tmp}/p{i:03d}.bin', 'wb') as b: a.seek(i * step); b.write(a.read(step + ov))
def scan(i):
    r = subprocess.run([cs, '--no-summary', '--allmatch', '--max-filesize=2000M', '--max-scansize=2000M', '--max-scantime=0', '-d', db,
                        f'{tmp}/p{i:03d}.bin'], capture_output=True, text=True)
    return [l.rsplit(': ', 1)[1][:-6] for l in r.stdout.splitlines() if l.endswith(' FOUND')]
with ThreadPoolExecutor(k) as ex: names = sorted({x for r in ex.map(scan, range(k)) for x in r})
for i in range(k): os.remove(f'{tmp}/p{i:03d}.bin')
os.rmdir(tmp); print('\n'.join(names))
