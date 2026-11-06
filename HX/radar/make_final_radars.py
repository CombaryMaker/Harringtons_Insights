#!/usr/bin/env python3
"""make_final_radars.py - final-ranking radar maps from tier-matrix-2026-11-06.csv (6 Nov 2026).
(c) 2026 Michael Hugh Harrington. usage: make_final_radars.py tier-matrix.csv OUTDIR
Chart 1: per data type, HX's best result at any speed vs the best other tool at any speed (size and decode time).
Chart 2: per speed tier (1-2-5 series), share of data types where HX is smaller / tied / larger than the best
other tool in that tier (HX may qualify from a faster tier; cells where nobody was measured are left out)."""
import sys, csv, math, collections
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; import numpy as np
R = list(csv.DictReader(open(sys.argv[1]))); out = sys.argv[2]
short = lambda t: t.split(',')[0].split('(')[0].split(':')[0 if not t.startswith(('Text', 'Programs', 'Scientific', 'Archives', 'Office', 'Already')) else 0].strip()[:24]
T = collections.OrderedDict()
for r in R: T.setdefault(r['data_type'], []).append(r)
rows = []
for t, rs in T.items():
    h = [r for r in rs if r['hx_ratio']]; o = [r for r in rs if r['other_ratio']]
    if not h or not o: continue
    bh = max(h, key=lambda r: float(r['hx_ratio'])); bo = max(o, key=lambda r: float(r['other_ratio']))
    size = 100 * float(bo['other_ratio']) / float(bh['hx_ratio'])          # HX bytes as % of best other's bytes
    dec = 100 * float(bo['other_decomp_MBps']) / float(bh['hx_decomp_MBps'])  # HX decode time as % of other's
    lab = t.replace('Text: server logs (16 systems), Chinese poetry, ', 'Text ').split(',')[0].split(' (')[0][:26]
    rows.append((lab, size, dec, bh['hx_tool'], bo['other_tool']))
n = len(rows); ang = np.linspace(0, 2 * math.pi, n, endpoint=False).tolist(); ang += ang[:1]
fig, axes = plt.subplots(1, 2, figsize=(24, 12), dpi=150, subplot_kw=dict(polar=True))
for ax, k, col, title, lim, ticks, labs, logs in [
    (axes[0], 1, '#1f77b4', 'COMPRESSED SIZE - HX as % of the best other tool\n(inside the dashed circle = HX is smaller)', (40, 160), [60, 80, 100, 120, 140], ['60%', '80%', '100%', '120%', '140%'], False),
    (axes[1], 2, '#d62728', 'DECODE TIME - HX as % of that tool, log scale\n(inside the dashed circle = HX decodes faster)', (-1, 3), [-1, 0, 1, 2, 3], ['0.1%', '1%', '10%', '100%', '1000%'], True)]:
    v = [math.log10(max(r[k], 0.1)) if logs else r[k] for r in rows]; v += v[:1]
    ax.set_theta_offset(math.pi / 2); ax.set_theta_direction(-1)
    ax.plot(ang, [2 if logs else 100] * (n + 1), color='#777', lw=1.3, ls='--')
    ax.plot(ang, v, color=col, lw=2.2, marker='o', ms=5); ax.fill(ang, v, color=col, alpha=0.12)
    ax.set_xticks(ang[:-1]); ax.set_xticklabels([f'{r[0]}\n{r[k]:.0f}%' for r in rows], fontsize=7.5)
    ax.set_ylim(*lim); ax.set_yticks(ticks); ax.set_yticklabels(labs, fontsize=8); ax.set_rlabel_position(0); ax.set_title(title, fontsize=12, pad=32)
w = sum(r[1] < 99.5 for r in rows); ti = sum(99.5 <= r[1] <= 100.5 for r in rows); f = sum(r[2] < 100 for r in rows)
plt.suptitle('HX - final rankings, 6 Nov 2026: HX\'s best vs the best other tool at any speed, per data type\nPatent Pending (U.S. App. 64/192,843)', fontsize=14, y=1.03)
plt.figtext(0.02, -0.01, f'{n} data types. Size: HX smaller on {w}, tie (within 0.5%) {ti}, other smaller {n - w - ti}. Decode: HX faster on {f} of {n}. '
            'Measured results only; "best other" = the smallest rival result on that type, at any speed. cmix/paq8px not included.', fontsize=9.5)
plt.savefig(f'{out}/HX-final-rankings-radar-best-any-speed-2026-11-06.png', bbox_inches='tight', facecolor='white'); plt.close()

# Chart 2 - per speed tier
tiers = collections.OrderedDict()
for r in R:
    if r['hx_ratio'] and r['other_ratio']:
        h, o = float(r['hx_ratio']), float(r['other_ratio']); s = 100 * o / h
        tiers.setdefault((int(r['tier']), r['M_upper']), []).append('W' if s < 99.5 else 'T' if s <= 100.5 else 'L')
keys = sorted(tiers); m = len(keys); a2 = np.linspace(0, 2 * math.pi, m, endpoint=False).tolist(); a2 += a2[:1]
fig, ax = plt.subplots(figsize=(11, 11), dpi=150, subplot_kw=dict(polar=True)); ax.set_theta_offset(math.pi / 2); ax.set_theta_direction(-1)
for code, col, name in [('W', '#2ca02c', 'HX smaller'), ('L', '#d62728', 'other tool smaller')]:
    v = [100 * tiers[k].count(code) / len(tiers[k]) for k in keys]; v += v[:1]
    ax.plot(a2, v, color=col, lw=2.2, marker='o', ms=5, label=name); ax.fill(a2, v, color=col, alpha=0.12)
ax.set_xticks(a2[:-1]); ax.set_xticklabels([f'Tier {k[0]} (M up to {k[1]})\nHX {tiers[k].count("W")}  tie {tiers[k].count("T")}  other {tiers[k].count("L")}  of {len(tiers[k])}' for k in keys], fontsize=8)
ax.set_ylim(0, 100); ax.set_yticks([25, 50, 75, 100]); ax.set_yticklabels(['25%', '50%', '75%', '100%'], fontsize=8); ax.set_rlabel_position(0)
ax.legend(loc='upper right', bbox_to_anchor=(1.18, 1.08))
ax.set_title('HX - final rankings by speed tier (1-2-5 series), 6 Nov 2026\nshare of data types where HX or the best other tool in that tier is smaller', fontsize=12, pad=34)
filled = sum(len(v) for v in tiers.values())
plt.figtext(0.02, 0.0, f'{filled} type x tier cells where both HX and a rival qualify (of {len(R)} cells; the rest are not tested or one side only). '
            'M = decode-speed multiple of the Compression Tier Standard v2.0.\nHX may qualify from a faster tier. Tie = within 0.5%. Patent Pending (U.S. App. 64/192,843).', fontsize=9)
plt.savefig(f'{out}/HX-final-rankings-radar-by-speed-tier-2026-11-06.png', bbox_inches='tight', facecolor='white')
print(n, 'types;', w, ti, n - w - ti, f, '|', [(k[0], tiers[k].count('W'), tiers[k].count('T'), tiers[k].count('L')) for k in keys])
