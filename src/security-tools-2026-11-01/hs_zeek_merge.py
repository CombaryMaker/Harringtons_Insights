# hs_zeek_merge.py - merge Zeek logs from Harrington Sets shard runs into the log set one Zeek process writes.
# Michael Hugh Harrington, 2026. MIT License.
# Shards are made by hs_pcapsplit --ticks (every shard's clock advances as in the plain run) and run with
# hs_shard.zeek loaded. Merge rules:
#  - every log: rows joined and put in time order (stable: shard order, then row order);
#  - software.log and known_services.log: rebuilt by replaying Zeek 8's own rules (base/frameworks/software/main.zeek,
#    policy/protocols/conn/known-services.zeek) over the raw inputs all shards recorded, in network-time order:
#      software: per host a table (expires 1 day after the host entry is created); an entry is logged when its name
#        is new for the host, its parsed version differs from the stored one, or force_log is set;
#      known_services: per (host, port) a set of services (expires 1 day after creation); an input is logged with the
#        services not yet in the set, unless all its services are already there and the entry exists;
#  - known_hosts / known_certs: a row is kept unless the same entry was logged in the 24 h before (Zeek's store expiry);
#  - shard self-health notices (CaptureLoss::*) dropped; hs_*_raw logs are inputs only and not written.
# Usage: hs_zeek_merge.py OUTDIR SHARD_DIR...
import os, sys, glob
from decimal import Decimal
out, shards = sys.argv[1], sys.argv[2:]; os.makedirs(out, exist_ok=True); DAY = Decimal(86400)
def read(path):
    head, fields, rows = [], [], []
    for line in open(path, errors='replace'):
        if line.startswith('#'):
            if not line.startswith('#close'): head.append(line)
            if line.startswith('#fields'): fields = line.rstrip('\n').split('\t')[1:]
        else: rows.append(line.rstrip('\n').split('\t'))
    return head, fields, rows
def T(v):
    try: return Decimal(v)
    except Exception: return Decimal(0)   # logs whose first column is not a time keep their shard/row order
def raw(name, delayed_ts_col=None):
    # Order = Zeek's processing order: by network time; at one network time, timers fire first (in order of their
    # scheduled time) and then the packet's own events. A known_services commit is a timer when its info.ts is
    # earlier than the network time it ran at (Zeek schedules a commit 5 min after a connection without a service).
    allrows = []; fields = []
    for si, s in enumerate(shards):
        p = f'{s}/{name}.log'
        if os.path.exists(p):
            h, fields, rows = read(p); c = fields.index(delayed_ts_col) if delayed_ts_col else None
            for ri, r in enumerate(rows):
                nt = T(r[0]); ts = T(r[c]) if c is not None else nt
                allrows.append(((nt, 0, ts) if ts < nt else (nt, 1, Decimal(0)), si, ri, r))
    allrows.sort(key=lambda x: x[:3]); return fields, [x[3] for x in allrows]
def header(name):
    for s in shards:
        if os.path.exists(f'{s}/{name}.log'): return read(f'{s}/{name}.log')[0:2]
    return None, None
names = sorted({os.path.basename(p)[:-4] for s in shards for p in glob.glob(s + '/*.log')})
for n in names:
    if n.startswith('hs_'): continue
    head, fields = header(n)
    if n == 'software' and any(os.path.exists(f'{s}/hs_software_raw.log') for s in shards):
        rf, rows = raw('hs_software_raw'); ix = {f: i for i, f in enumerate(rf)}; tracked = {}; outrows = []
        vcols = [ix['info.version.' + v] for v in ('major', 'minor', 'minor2', 'minor3', 'addl')]
        for r in rows:
            nt = T(r[0]); host = r[ix['info.host']]; name = r[ix['info.name']]; ver = tuple(r[i] for i in vcols)
            if host in tracked and nt >= tracked[host][0] + DAY: del tracked[host]
            created, ts = tracked.setdefault(host, (nt, {}))
            if name in ts and ts[name] == ver and r[ix['force_log']] != 'T': continue
            ts[name] = ver; outrows.append('\t'.join(r[ix['info.' + f]] for f in fields))
    elif n == 'known_services' and any(os.path.exists(f'{s}/hs_known_services_raw.log') for s in shards):
        rf, rows = raw('hs_known_services_raw', 'info.ts'); ix = {f: i for i, f in enumerate(rf)}; services = {}; outrows = []   # key: host, port number AND protocol (53/tcp and 53/udp are different ports in Zeek)
        for r in rows:
            nt = T(r[0]); key = (r[ix['info.host']], r[ix['info.port_num']], r[ix['info.port_proto']])
            sv = r[ix['info.service']]; cnt = int(r[ix['n']]); new = set() if cnt == 0 else set(sv.split(','))
            if key in services and nt >= services[key][0] + DAY: del services[key]
            if key in services and new <= services[key][1]: continue
            created, have = services.setdefault(key, (nt, set())); add = new - have; have |= add
            vals = {'ts': r[ix['info.ts']], 'host': key[0], 'port_num': key[1], 'port_proto': r[ix['info.port_proto']],
                    'service': ','.join(sorted(add)) if add else '(empty)'}
            outrows.append('\t'.join(vals[f] for f in fields))
    else:
        _, rows = raw(n); outrows = ['\t'.join(r) for r in rows]
        if n in ('known_hosts', 'known_certs'):
            last = {}; keep = []
            for line in outrows:
                ts, key = line.split('\t', 1); t = T(ts)
                if key in last and t < last[key] + DAY: continue
                last[key] = t; keep.append(line)
            outrows = keep
        if n == 'notice': outrows = [l for l in outrows if '\tCaptureLoss::' not in l]
    with open(f'{out}/{n}.log', 'w') as f: f.writelines(head + [l + '\n' for l in outrows])
