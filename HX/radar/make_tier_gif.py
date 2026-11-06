#!/usr/bin/env python3
"""make_tier_gif.py - animated radar GIF, one frame per speed tier, 29 data types per frame.
(c) 2026 Michael Hugh Harrington. usage: make_tier_gif.py final-rankings.csv tier-matrix.csv OUT.gif
Blue = compression: best ratio of Michael's software / best ratio of any other tool, both at this speed or faster.
Green = decode speed of those same two tools (ours / theirs). Log scale, red ring = 100% (equal).
Outside the red ring = Michael's software is better. Gold H = Michael's software is the best (smallest) in that category.
Same data and rules as Top_Performers (same-machine campaign 3-4 Nov 2026)."""
import sys, csv, math, collections, io
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; import numpy as np
from matplotlib.patches import Circle
from PIL import Image
rank, mat, out = sys.argv[1:4]
OURS = {'ours', 'modified', 'mutant/research'}; EXCL = ('precomp', 'mzip', 'x264')
NAMES = {'Biased':'Biased test files','NIST':'Real noise (NIST)','Random':'Random Turing output','Text':'Text, logs, CSV',
 'Programs':'Programs (EXE)','Silesia':'Silesia','Calgary':'Calgary','Canterbury corpus':'Canterbury','Canterbury large':'Canterbury large',
 'Synthetic':'Synthetic files','Speech':'Speech audio','Music':'Music audio','Environmental':'Environmental audio',
 'Already-compressed audio':'MP3/AAC/Opus','Colour':'Colour photos','Rendered':'Document pages','Medical':'Medical DICOM',
 'PNG':'PNG images','JPEG':'JPEG photos','GIF':'GIF and TIFF','Raw video':'Raw video','Compressed video':'Compressed video',
 'PDF':'PDF files','Packet captures':'Packet captures','Packet capture':'One big pcap','Office':'Office files',
 'SQLite':'SQLite database','Archives':'Archives (ZIP/TAR)','Scientific':'Scientific data'}
def nm(t):
    for k in sorted(NAMES, key=len, reverse=True):
        if t.startswith(k): return NAMES[k]
    return t[:20]
M = list(csv.DictReader(open(mat))); types = list(dict.fromkeys(r['data_type'] for r in M)); ref, upper = {}, {}
for r in M:
    upper[int(r['tier'])] = float(r['M_upper']) if r['M_upper'] else None
    for a, b in (('hx_M', 'hx_decomp_MBps'), ('other_M', 'other_decomp_MBps')):
        if r[a] and r[b] and r['data_type'] not in ref: ref[r['data_type']] = float(r[a]) * float(r[b])
rows = collections.defaultdict(dict)
for r in csv.DictReader(open(rank)):
    if 'same-machine campaign 3-4 Nov 2026' not in r['corpus'] or not r['ratio'] or not r['decomp_MBps']: continue
    t = r['corpus'].split(' | ', 1)[1].replace(' - same-machine campaign 3-4 Nov 2026', '')
    if r['group'] in OURS and any(x in r['tool'].lower() for x in EXCL): continue
    v = dict(tool=r['tool'], ours=r['group'] in OURS, ratio=float(r['ratio']), dec=float(r['decomp_MBps']))
    if r['tool'] not in rows[t] or v['ratio'] > rows[t][r['tool']]['ratio']: rows[t][r['tool']] = v
LO, HI = math.log10(25), math.log10(400)          # radial range 25% .. 400%, log scale
rr = lambda pct: float('nan') if pct != pct else 0.14 + 0.86 * (min(max(math.log10(max(pct, 1e-9)), LO), HI) - LO) / (HI - LO)   # inner hole: 25% or less sits on the inner ring
n = len(types); ang = np.linspace(0, 2 * math.pi, n, endpoint=False)
frames, total_h = [], 0
def frame(k):
    global total_h
    u = upper[k]; comp, spd, wins = [], [], []
    for t in types:
        ok = [v for v in rows[t].values() if u is None or ref[t] / v['dec'] <= u]
        o = max([v for v in ok if v['ours']], key=lambda v: v['ratio'], default=None)
        x = max([v for v in ok if not v['ours']], key=lambda v: v['ratio'], default=None)
        best = max(ok, key=lambda v: v['ratio'], default=None)
        wins.append(bool(best and best['ours']))
        if o and x: comp.append(100 * o['ratio'] / x['ratio']); spd.append(100 * o['dec'] / x['dec'])
        elif o: comp.append(400); spd.append(400)          # no rival this fast: ours alone
        else: comp.append(float('nan')); spd.append(float('nan'))   # none of ours this fast: gap in the lines
    total_h += sum(wins)
    fig = plt.figure(figsize=(16, 16), dpi=100); ax = fig.add_axes([0.17, 0.14, 0.66, 0.72], polar=True)
    ax.set_theta_offset(math.pi / 2); ax.set_theta_direction(-1); ax.set_ylim(0, 1.0); ax.set_yticks([])
    for p in (25, 50, 200, 400):
        ax.plot(np.linspace(0, 2 * math.pi, 400), [rr(p)] * 400, color='#c8c8c8', lw=1)
        ax.text(math.radians(4), rr(p), ('25% or less' if p == 25 else f'{p}%'), fontsize=12, color='#777', ha='left', va='bottom')
    ax.plot(np.linspace(0, 2 * math.pi, 400), [rr(100)] * 400, color='#e00000', lw=4.5, zorder=3)
    ax.text(math.radians(4), rr(100) + 0.01, '100% = EQUAL', fontsize=14, color='#e00000', fontweight='bold', ha='left', va='bottom')
    A = list(ang) + [ang[0]]
    ax.plot(A, [rr(c) for c in comp] + [rr(comp[0])], color='#1565c0', lw=3.5, marker='o', ms=9, zorder=4, label='Blue = compression (ours / best other)')
    ax.plot(A, [rr(v) for v in spd] + [rr(spd[0])], color='#2e9e3a', lw=3.5, marker='s', ms=8, zorder=4, label='Green = decode speed (ours / that tool)')
    ax.set_xticks(ang); ax.set_xticklabels([])
    for a, t in zip(ang, types):
        dx = math.sin(a); ha = 'center' if abs(dx) < 0.1 else ('left' if dx > 0 else 'right')
        ax.text(a, 1.16, nm(t), fontsize=14, ha=ha, va='center', clip_on=False)
    ax.grid(axis='x', color='#dddddd'); ax.spines['polar'].set_color('#999')
    for a, w in zip(ang, wins):
        if w:
            ax.text(a, 1.065, 'H', fontsize=24, fontweight='bold', fontstyle='italic', family='DejaVu Serif', color='#7a5200',
                    ha='center', va='center', zorder=6, clip_on=False,
                    bbox=dict(boxstyle='circle,pad=0.22', fc='#ffd54a', ec='#8a6000', lw=2))
    lim = f'decode at least 1/{u:g} of LZ4\'s speed' if u else 'any speed (slowest tier)'
    fig.suptitle(f'SPEED TIER {k} of 10  -  {lim}', fontsize=30, fontweight='bold', y=0.993)
    fig.text(0.5, 0.952, f'Michael Harrington\'s software is BEST in {sum(wins)} of {n} data types  (gold H = best)',
             ha='center', fontsize=22, color='#7a5200', fontweight='bold')
    fig.legend(loc='lower center', bbox_to_anchor=(0.5, 0.035), ncol=2, fontsize=16, frameon=False)
    fig.text(0.5, 0.008, 'Outside the red ring = ours is better.  Log scale, clipped at 25% and 400%.  A gap = none of our tools is this fast.  '
             'Same machine, same session (3-4 Nov 2026).\nHX is Patent Pending (U.S. App. 64/192,843).   github.com/CombaryMaker/harringtons_insights', ha='center', fontsize=13, color='#444')
    b = io.BytesIO(); fig.savefig(b, format='png', facecolor='white'); plt.close(fig); b.seek(0)
    return Image.open(b).convert('RGB')
for k in sorted(upper): frames.append(frame(k))
# closing frame: totals
fig = plt.figure(figsize=(16, 16), dpi=100); fig.patch.set_facecolor('white')
fig.text(0.5, 0.68, f'{total_h} of {n * len(upper)}', ha='center', fontsize=110, fontweight='bold', color='#7a5200')
fig.text(0.5, 0.58, 'categories won outright', ha='center', fontsize=40)
fig.text(0.5, 0.48, f'{n} data types  x  {len(upper)} speed tiers', ha='center', fontsize=30, color='#444')
fig.text(0.5, 0.36, 'Michael Hugh Harrington  -  HX', ha='center', fontsize=34, fontweight='bold')
fig.text(0.5, 0.30, 'Patent Pending (U.S. App. 64/192,843)', ha='center', fontsize=22, color='#444')
fig.text(0.5, 0.24, 'github.com/CombaryMaker/harringtons_insights', ha='center', fontsize=22, color='#1565c0')
b = io.BytesIO(); fig.savefig(b, format='png', facecolor='white'); plt.close(fig); b.seek(0); frames.append(Image.open(b).convert('RGB'))
pal = [f.quantize(colors=128, method=Image.Quantize.MEDIANCUT) for f in frames]
pal[0].save(out, save_all=True, append_images=pal[1:], duration=[3000] * (len(pal) - 1) + [4000], loop=0, optimize=True)
frames[0].save(out.replace('.gif', '-tier1.png')); print(total_h, 'H marks;', len(pal), 'frames')
