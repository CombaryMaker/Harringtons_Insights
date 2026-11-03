#!/usr/bin/env python3
"""summary.py - totals per test set and tool from results.csv (Mahoney test sets race).
Michael Hugh Harrington, 2026. MIT License."""
import csv, sys
from collections import OrderedDict

NAMES = {'zpaq_m4': 'ZPAQ 7.15 -m4', 'zpaq_m5': 'ZPAQ 7.15 -m5', 'lpaq1_9': 'lpaq1 9',
         'hcm_fast': 'HCM fast', 'hcm_max': 'HCM max', 'hcm_sets_fast': 'HCM Sets fast',
         'hcm_sets_max': 'HCM Sets max', 'hcmx_max': 'hcmx max', 'lpaq1h_9': 'lpaq1-h 9',
         'zpaqhi_m5': 'zpaq-hi -m5'}
MAHONEY = {'zpaq_m4', 'zpaq_m5', 'lpaq1_9'}
SETS = {'calgary': 'Calgary corpus (18 files, each compressed on its own)',
        'large': 'Canterbury large corpus (E.coli, bible.txt, world192.txt)',
        'gcb': 'Generic Compression Benchmark (uiq2 seeds 0, 1, 2)',
        'enwik9': 'enwik9 (1,000,000,000 bytes)'}

rows = list(csv.DictReader(open(sys.argv[1])))
tot = OrderedDict()
for r in rows:
    k = (r['set'], r['tool'])
    t = tot.setdefault(k, dict(orig=0, comp=0, ct=0.0, dt=0.0, cm=0, dm=0, files=0, exact=True))
    t['orig'] += int(r['orig_bytes']); t['comp'] += int(r['comp_bytes'])
    # HCM checks its own output by decoding it before it exits, so its compress time is the wall time minus one decode.
    # (Its printed time cannot be used: in Sets mode the check time is summed over all threads.)
    cw, dw = float(r['c_wall_s']), float(r['d_wall_s'])
    t['ct'] += cw - dw if r['tool'].startswith('hcm') else cw; t['dt'] += dw
    t['cm'] = max(t['cm'], int(r['c_mem_kb'])); t['dm'] = max(t['dm'], int(r['d_mem_kb']))
    t['files'] += 1; t['exact'] &= r['exact'] == 'yes'

print('MATT MAHONEY TEST SETS RACE - totals (sizes in bytes; ratio = original / compressed)')
print('Mahoney\'s programs are marked *. Times are wall clock; HCM compress time = wall time minus its built-in check decode.')
for s, title in SETS.items():
    items = [(tool, t) for (ss, tool), t in tot.items() if ss == s]
    if not items: continue
    items.sort(key=lambda x: x[1]['comp'])
    print(f'\n{title}')
    print(f'  {"program":<16}{"compressed":>14}{"ratio":>8}{"compress s":>12}{"decode s":>10}{"peak MB":>9}  exact')
    for tool, t in items:
        mark = '*' if tool in MAHONEY else ' '
        print(f' {mark}{NAMES.get(tool, tool):<16}{t["comp"]:>14,}{t["orig"] / max(1, t["comp"]):>8.3f}{t["ct"]:>12.1f}'
              f'{t["dt"]:>10.1f}{max(t["cm"], t["dm"]) / 1024:>9.0f}  {"yes" if t["exact"] else "NO"} ({t["files"]} files)')
    best_m = min((t['comp'], tool) for tool, t in items if tool in MAHONEY) if any(tool in MAHONEY for tool, _ in items) else None
    best_o = min((t['comp'], tool) for tool, t in items if tool not in MAHONEY) if any(tool not in MAHONEY for tool, _ in items) else None
    if best_m and best_o:
        verdict = 'OURS SMALLER' if best_o[0] < best_m[0] else 'MAHONEY SMALLER'
        print(f'  Best of Mahoney: {NAMES[best_m[1]]} {best_m[0]:,} | best of ours: {NAMES[best_o[1]]} {best_o[0]:,} -> {verdict} '
              f'by {abs(best_m[0] - best_o[0]):,} bytes ({abs(best_m[0] - best_o[0]) / best_m[0] * 100:.2f}%)')
