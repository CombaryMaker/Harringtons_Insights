# zeekcmp.py - compare Zeek logs from a plain run (DIR_A) with merged shard logs (DIR_B). MIT License.
# Per log: rows compared as sorted multisets after dropping connection-ID columns (uid, conn_uids: random per process);
# set/vector columns compared with elements sorted (each Zeek process prints set elements in its own hash order).
# Logs about the Zeek process itself (stats, loaded_scripts, packet_filter, capture_loss, reporter, telemetry) skipped.
import os, sys, glob, collections
A, B = sys.argv[1], sys.argv[2]; SKIP = {'stats', 'loaded_scripts', 'packet_filter', 'capture_loss', 'reporter', 'telemetry'}
def rows(p):
    out = collections.Counter(); keep = []; sets = set()
    if not os.path.exists(p): return out
    for line in open(p, errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('#fields'): keep = [i for i, f in enumerate(line.split('\t')[1:]) if not (f.endswith('uid') or f.endswith('uids'))]
        elif line.startswith('#types'): sets = {i for i, t in enumerate(line.split('\t')[1:]) if t.startswith(('set[', 'vector['))}
        elif line and not line.startswith('#'):
            c = line.split('\t'); c = [','.join(sorted(v.split(','))) if i in sets else v for i, v in enumerate(c)]
            out['\t'.join(c[i] for i in keep if i < len(c))] += 1
    return out
names = sorted({os.path.basename(p)[:-4] for p in glob.glob(A + '/*.log') + glob.glob(B + '/*.log')} - SKIP)
same = tot = 0
for n in names:
    a = rows(f'{A}/{n}.log'); b = rows(f'{B}/{n}.log'); na, nb = sum(a.values()), sum(b.values())
    po, so = sum((a - b).values()), sum((b - a).values()); tot += na; same += na - po
    print(f'{n:22} plain {na:>8,}  split {nb:>8,}  ' + ('IDENTICAL' if po == so == 0 else f'differ: {po} plain-only, {so} split-only rows'))
print(f'ALL LOGS: {same:,} of {tot:,} plain rows matched exactly')
