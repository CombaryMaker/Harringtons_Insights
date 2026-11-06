#!/usr/bin/env python3
"""make_type_radars.py - large-text radar maps with every data type as a spoke, from tier-matrix-2026-11-06.csv.
(c) 2026 Michael Hugh Harrington. usage: make_type_radars.py tier-matrix.csv OUTDIR
1) per type: % of speed tiers where HX is smaller than the best other tool in that tier
2) per type: HX's best size as % of the best other tool's size (any speed)
3) per type: HX's decode time as % of that tool's decode time (log scale)"""
import sys, csv, math, collections
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; import numpy as np
R = list(csv.DictReader(open(sys.argv[1]))); out = sys.argv[2]
NAMES = {'Biased':'Biased test files','NIST':'Real noise (NIST)','Random':'Random Turing output','Text':'Text, logs, CSV',
 'Programs':'Programs (EXE)','Silesia':'Silesia','Calgary':'Calgary','Canterbury corpus':'Canterbury','Canterbury large':'Canterbury large',
 'Synthetic':'Synthetic files','Speech':'Speech audio','Music':'Music audio','Environmental':'Environmental audio',
 'Already-compressed audio':'MP3/AAC/Opus','Colour':'Colour photos','Rendered':'Document pages','Medical':'Medical DICOM',
 'PNG':'PNG images','JPEG':'JPEG photos','GIF':'GIF and TIFF','Raw video':'Raw video','Compressed video':'Compressed video',
 'PDF':'PDF files','Packet captures':'Packet captures','Packet capture':'One big pcap','Office':'Office files',
 'SQLite':'SQLite database','Archives':'Archives (ZIP/TAR)','Scientific':'Scientific data'}
def name(t):
    for k in sorted(NAMES, key=len, reverse=True):
        if t.startswith(k): return NAMES[k]
    return t[:20]
T = collections.OrderedDict()
for r in R: T.setdefault(r['data_type'], []).append(r)
rows = []
for t, rs in T.items():
    both = [r for r in rs if r['hx_ratio'] and r['other_ratio']]
    w = sum(100 * float(r['other_ratio']) / float(r['hx_ratio']) < 99.5 for r in both)
    ti = sum(99.5 <= 100 * float(r['other_ratio']) / float(r['hx_ratio']) <= 100.5 for r in both)
    h = [r for r in rs if r['hx_ratio']]; o = [r for r in rs if r['other_ratio']]
    bh = max(h, key=lambda r: float(r['hx_ratio'])); bo = max(o, key=lambda r: float(r['other_ratio']))
    rows.append(dict(n=name(t), wins=w, ties=ti, cells=len(both), size=100 * float(bo['other_ratio']) / float(bh['hx_ratio']),
                     dec=100 * float(bo['other_decomp_MBps']) / float(bh['hx_decomp_MBps'])))
n = len(rows); ang = np.linspace(0, 2 * math.pi, n, endpoint=False).tolist(); ang += ang[:1]
def radar(vals, labels, ref, ylim, ticks, tlabs, col, title, foot, fn):
    fig, ax = plt.subplots(figsize=(16, 16), dpi=110, subplot_kw=dict(polar=True))
    ax.set_theta_offset(math.pi / 2); ax.set_theta_direction(-1)
    v = vals + vals[:1]; ax.plot(ang, [ref] * (n + 1), color='#555', lw=2, ls='--')
    ax.plot(ang, v, color=col, lw=3, marker='o', ms=8); ax.fill(ang, v, color=col, alpha=0.15)
    ax.set_xticks(ang[:-1]); ax.set_xticklabels(labels, fontsize=14); ax.tick_params(axis='x', pad=22)
    ax.set_ylim(*ylim); ax.set_yticks(ticks); ax.set_yticklabels(tlabs, fontsize=13, color='#444'); ax.set_rlabel_position(4)
    ax.set_title(title, fontsize=21, pad=60, fontweight='bold')
    fig.text(0.5, 0.02, foot, ha='center', fontsize=14, wrap=True)
    plt.savefig(f'{out}/{fn}', bbox_inches='tight', facecolor='white'); plt.close()
P = 'Patent Pending (U.S. App. 64/192,843)'
W = sum(r['wins'] for r in rows); C = sum(r['cells'] for r in rows); Ti = sum(r['ties'] for r in rows)
radar([100 * r['wins'] / r['cells'] if r['cells'] else 0 for r in rows],
      [f"{r['n']}\nHX wins {r['wins']} of {r['cells']}" if r['cells'] else f"{r['n']}\n(no head-to-head)" for r in rows],
      50, (0, 100), [25, 50, 75, 100], ['25%', '50%', '75%', '100%'], '#2ca02c',
      'HX vs the best other tool, every data type\n% of speed tiers where HX is smaller',
      f'{n} data types x 10 speed tiers. HX smaller in {W}, tied in {Ti}, of the {C} type-and-tier cells where both were measured.\n'
      f'Outside the dashed circle = HX wins more than half of that type\'s tiers. {P}', 'HX-radar-1-wins-per-type-2026-11-06.png')
radar([r['size'] for r in rows], [f"{r['n']}\n{r['size']:.0f}%" for r in rows], 100, (40, 160),
      [60, 80, 100, 120, 140], ['60%', '80%', '100%', '120%', '140%'], '#1f77b4',
      'Compressed size: HX\'s best vs the best other tool\n(HX size as % of theirs, any speed)',
      f'Inside the dashed circle = HX is smaller. HX smaller on {sum(r["size"] < 99.5 for r in rows)}, tied on '
      f'{sum(99.5 <= r["size"] <= 100.5 for r in rows)}, larger on {sum(r["size"] > 100.5 for r in rows)} of {n} types. {P}',
      'HX-radar-2-size-per-type-2026-11-06.png')
radar([math.log10(max(r['dec'], 0.1)) for r in rows], [f"{r['n']}\n{r['dec']:.0f}%" for r in rows], 2, (-1, 3),
      [0, 1, 2, 3], ['1%', '10%', '100%', '1000%'], '#d62728',
      'Decode time: HX vs that same best other tool\n(HX time as % of theirs, log scale)',
      f'Inside the dashed circle = HX decodes faster. HX faster on {sum(r["dec"] < 100 for r in rows)} of {n} types. {P}',
      'HX-radar-3-decode-per-type-2026-11-06.png')
for r in rows: print(r['n'], r['wins'], r['ties'], r['cells'])
