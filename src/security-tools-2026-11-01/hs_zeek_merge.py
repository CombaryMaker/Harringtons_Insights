# hs_zeek_merge.py - merge Zeek logs from shard runs into one log set, as one Zeek process would have written it.
# Michael Hugh Harrington, 2026. MIT License.
# Rows of each log are joined and put in time order. Logs that Zeek writes "once per item per day" from a process-wide
# table (known_hosts, known_services, known_certs) keep a row only if the same item was not logged in the 24 h before
# (Zeek's default store expiry); software.log the same (Zeek re-logs an entry after its 24 h store expiry). Shard self-health notices
# (CaptureLoss::*) are dropped; the plain run's process-health logs are not compared.
# Usage: hs_zeek_merge.py OUTDIR SHARD_DIR...
import os, sys, glob
out, shards = sys.argv[1], sys.argv[2:]; os.makedirs(out, exist_ok=True)
names = sorted({os.path.basename(p)[:-4] for s in shards for p in glob.glob(s + '/*.log')})
for n in names:
    head = []; rows = []
    for s in shards:
        p = f'{s}/{n}.log'
        if not os.path.exists(p): continue
        h = []
        for line in open(p, errors='replace'):
            if line.startswith('#'):
                if not line.startswith('#close'): h.append(line)
            else: rows.append(line)
        head = head or h
    rows.sort(key=lambda r: float(r.split('\t', 1)[0]) if r[0].isdigit() else 0.0)
    if n in ('known_hosts', 'known_services', 'known_certs', 'software'):
        last = {}; keep = []
        for r in rows:
            ts, key = r.split('\t', 1); t = float(ts)
            if key in last and t < last[key] + 86400: continue
            last[key] = t; keep.append(r)
        rows = keep
    if n == 'notice': rows = [r for r in rows if '\tCaptureLoss::' not in r]
    with open(f'{out}/{n}.log', 'w') as f: f.writelines(head + rows)
