#!/usr/bin/env python3
"""tier_sensitivity.py - recount Top_Performers with 5, 10 and 20 speed tiers (same data, same rules).
(c) 2026 Michael Hugh Harrington. usage: tier_sensitivity.py final-rankings.csv tier-matrix.csv"""
import sys, csv, collections
rank, mat = sys.argv[1:3]; OURS = {'ours', 'modified', 'mutant/research'}; EXCL = ('precomp', 'mzip', 'x264')
M = list(csv.DictReader(open(mat))); types = list(dict.fromkeys(r['data_type'] for r in M)); ref = {}
for r in M:
    for a, b in (('hx_M', 'hx_decomp_MBps'), ('other_M', 'other_decomp_MBps')):
        if r[a] and r[b] and r['data_type'] not in ref: ref[r['data_type']] = float(r[a]) * float(r[b])
rows = collections.defaultdict(dict)
for r in csv.DictReader(open(rank)):
    if 'same-machine campaign 3-4 Nov 2026' not in r['corpus'] or not r['ratio'] or not r['decomp_MBps']: continue
    t = r['corpus'].split(' | ', 1)[1].replace(' - same-machine campaign 3-4 Nov 2026', '')
    if r['group'] in OURS and any(x in r['tool'].lower() for x in EXCL): continue
    v = dict(tool=r['tool'], ours=r['group'] in OURS, ratio=float(r['ratio']), dec=float(r['decomp_MBps']))
    if r['tool'] not in rows[t] or v['ratio'] > rows[t][r['tool']]['ratio']: rows[t][r['tool']] = v
SCHEMES = {'5 tiers (decades: M <= 10, 100, 1000, 10000, any)': [10, 100, 1000, 10000, None],
           '10 tiers (published, 1-2-5)': [2, 5, 10, 20, 50, 100, 200, 500, 2000, None],
           '20 tiers (1-1.5-2-3-5-7 series)': [1.5, 2, 3, 5, 7, 10, 15, 20, 30, 50, 70, 100, 150, 200, 300, 500, 700, 1000, 2000, None]}
for name, U in SCHEMES.items():
    first, second, n = collections.Counter(), 0, 0; nl = collections.Counter()
    for t in types:
        tools = sorted(rows[t].values(), key=lambda v: -v['ratio'])
        for i, u in enumerate(U):
            ok = [v for v in tools if u is None or ref[t] / v['dec'] <= u]; a, b = (ok + [None, None])[:2]; n += 1
            if a: first['YOURS' if a['ours'] else a['tool']] += 1
            if a and u is not None: nl['YOURS' if a['ours'] else a['tool']] += 1
            if b and b['ours']: second += 1
    tot = n; nt = n - len(types)
    print(f'\n{name}: {tot} categories')
    print(f'  yours first {first["YOURS"]} ({100*first["YOURS"]/tot:.1f}%), second {second} ({100*second/tot:.1f}%); '
          f'without the any-speed tier: first {nl["YOURS"]} of {nt} ({100*nl["YOURS"]/nt:.1f}%)')
    for k, v in first.most_common(5): print(f'    {v:4d} {100*v/tot:5.1f}%  {k}')
