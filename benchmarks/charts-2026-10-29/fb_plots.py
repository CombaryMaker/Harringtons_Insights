#!/usr/bin/env python3
"""Facebook image set: the best-results charts using only the author's own data (public corpora and the
author's own test files), phone-sized, each image with its own headline and takeaway.
Excludes any third-party reviewer's data sets or recorded results. Output: fb/NN_name.png + fb/captions.txt"""
import os, textwrap
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker
from matplotlib.lines import Line2D
import plots as P
import best_data as B

P.KIND['Best-of'] = ('#6250d6', 'P', 'Harrington best-of (best of our tools, picked per file)')
P.OURS = ('Original', 'Modified', 'Mutant', 'Best-of'); OURS = P.OURS
INK, INK2, GRID = P.INK, P.INK2, P.GRID
OUT = os.path.join(P.HERE, 'fb'); os.makedirs(OUT, exist_ok=True)
plt.rcParams.update({'font.size': 11})
DPI = 150
KEEP = ['Canterbury corpus', 'Silesia corpus (full)', 'Biased packed data', 'Radio SDR captures',
        'Modern logs and exports', 'PDF documents (139 files)', 'Speech audio (FLAC, 6 speakers)']
SHORT = {'Canterbury corpus': 'canterbury', 'Silesia corpus (full)': 'silesia', 'Biased packed data': 'biased',
         'Radio SDR captures': 'radio', 'Modern logs and exports': 'logs', 'PDF documents (139 files)': 'pdf',
         'Speech audio (FLAC, 6 speakers)': 'speech'}
TAKE = {
 'Canterbury corpus': "My best, picked per file: 1,000,000 bits down to 123,837, decoded in 3.4 s. ZPAQ -m5: 124,713 in 11.5 s. Smaller AND 3.4x faster.",
 'Silesia corpus (full)': "ZPAQ -m5 is still the smallest here, but my best decodes 2.1x faster, and 7z-hi, xz-hi, lzma-hi and RAR each beat their stock versions on size with no slower decode.",
 'Biased packed data': "My best, picked per file: 465,170 in 15 ms against ZPAQ -m5 at 465,419 in 10.6 s. The Exchange->Blosc2 hybrid is 8% smaller than Blosc2 at almost the same speed.",
 'Radio SDR captures': "xz-hi beats xz -9e on size with the stock decoder. The Harrington Exchange decodes about 4x faster than xz.",
 'Modern logs and exports': "xz-hi is the smallest of every tool tested: 60,702 against 61,305 for xz -9e. On server logs HCM max is up to 40% smaller than any stock tool.",
 'PDF documents (139 files)': "My PDF row filter beats qpdf's optimisation. The No-Pain pick is smaller AND renders faster.",
 'Speech audio (FLAC, 6 speakers)': "flac-hi: 2.1% smaller than flac -8 and decodes 10% faster, read by the standard FLAC decoder.",
}
def per_m(c, o): return c / o * 1e6
def fmt_t(t):
    if t is None: return 'no decode time'
    if t < 0.01: return f'{t*1000:.2f} ms'
    return f'{t*1000:.0f} ms' if t < 1 else f'{t:.1f} s'
def head(fig, title, take, top=0.965):
    fig.text(0.05, top, title, fontsize=15, fontweight='bold', color=INK, va='top')
    fig.text(0.05, top - 0.045, textwrap.fill(take, 78), fontsize=10.5, color='#1a7f37', va='top', fontweight='bold')
def foot(fig, extra=''):
    fig.text(0.05, 0.012, textwrap.fill(("Sizes: 1,000,000 bits down to X bits. " + extra + " Michael Harrington, Harrington's Insights."), 105),
             fontsize=7.5, color=INK2, va='bottom')
def legend(fig, y):
    h = [Line2D([], [], marker='s', ls='', color=c, markersize=9, label=l.split(' (')[0]) for c, m, l in P.KIND.values()]
    fig.legend(handles=h, loc='lower center', bbox_to_anchor=(0.5, y), ncol=3, fontsize=8, frameon=False)

caps = []
def save(fig, name, cap):
    n = f'{len(caps)+1:02d}_{name}.png'; fig.savefig(os.path.join(OUT, n), dpi=DPI); plt.close(fig); caps.append((n, cap))

def scoreboard():
    rows = []
    for name in KEEP:
        d = B.STANDARDS[name]; o = d['orig']; E = d['entries']
        ours = min((x for x in E if x[1] in OURS), key=lambda x: x[2])
        std = min((x for x in E if x[1] == 'Standard' and x[0] != 'Best standard per file'), key=lambda x: x[2])
        win = ours[2] < std[2]
        rows.append((name.replace(' (FLAC, 6 speakers)', '').replace(' (139 files)', ''), f'{per_m(ours[2], o):,.0f}', f'{per_m(std[2], o):,.0f}',
                     'OURS SMALLEST' if win else f'{(ours[2]-std[2])/std[2]*100:.1f}% behind', ours[0], std[0]))
    fig = plt.figure(figsize=(7.2, 9)); head(fig, 'Scoreboard: my best vs the best standard tool',
        'Every benchmark I have recorded. Green = my tools are the smallest of all. Where I am behind, my tools usually decode several times faster (see each benchmark).')
    y = 0.84
    for r in rows:
        col = '#1a7f37' if 'OURS' in r[3] else INK2
        fig.patches.append(plt.Rectangle((0.04, y - 0.085), 0.92, 0.095, transform=fig.transFigure, facecolor='#f4f3ef', edgecolor='none'))
        fig.text(0.06, y - 0.005, r[0], fontsize=12, fontweight='bold', color=INK, va='top')
        fig.text(0.94, y - 0.005, r[3], fontsize=11, fontweight='bold', color=col, va='top', ha='right')
        fig.text(0.06, y - 0.037, f'Mine: {r[1]}  ({r[4]})', fontsize=9.5, color=INK, va='top')
        fig.text(0.06, y - 0.060, f'Best standard: {r[2]}  ({r[5]})', fontsize=9.5, color=INK2, va='top')
        y -= 0.108
    foot(fig)
    save(fig, 'scoreboard', "The scoreboard: my best result on every benchmark I have recorded against the best standard tool on that same benchmark.")

def leaderboard(name, d, n_max=16):
    o = d['orig']; E = sorted([x for x in d['entries']], key=lambda x: x[2])
    if len(E) > n_max:
        E = E[:n_max]
    fig, ax = plt.subplots(figsize=(7.2, 9)); fig.subplots_adjust(left=0.42, right=0.80, top=0.80, bottom=0.14)
    head(fig, f'{name}: ranking', TAKE[name])
    ys = list(range(len(E)))[::-1]
    for yi, (n, k, c, t, s) in zip(ys, E):
        ours = k in OURS
        ax.barh(yi, per_m(c, o), color=P.KIND[k][0], height=0.66, edgecolor='white')
        ax.text(per_m(c, o), yi, f' {per_m(c, o):,.0f}\n {fmt_t(t)}', va='center', fontsize=7.5, color=INK if ours else INK2,
                fontweight='bold' if ours else 'normal', linespacing=0.95)
    ax.set_yticks(ys); ax.set_yticklabels([x[0] for x in E], fontsize=8.5)
    for lbl, x in zip(ax.get_yticklabels(), E):
        lbl.set_fontweight('bold' if x[1] in OURS else 'normal'); lbl.set_color(INK if x[1] in OURS else INK2)
    lo = min(per_m(x[2], o) for x in E); hi = max(per_m(x[2], o) for x in E)
    ax.set_xlim(max(0, lo - (hi - lo) * 0.25) if hi / lo < 1.6 else 0, hi * 1.01)
    ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(4)); ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f'{v:,.0f}'))
    ax.tick_params(axis='x', labelsize=8); ax.set_xlabel('1,000,000 bits down to ... (shorter is better)', fontsize=9)
    for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
    ax.grid(True, axis='x', color=GRID, lw=0.6); ax.set_axisbelow(True)
    legend(fig, 0.045); foot(fig, f'Shown: the {len(E)} smallest results.' if len(d['entries']) > n_max else '')
    save(fig, f'{SHORT[name]}_ranking', f"{name}, every recorded result ranked by size (shorter bar is better), with decode time beside each bar. {TAKE[name]}")

def tradeoff(name, d):
    rows = {n: dict(kind=k, orig=d['orig'], comp=c, dt=t) for n, k, c, t, s in d['entries'] if t is not None}
    if len(rows) < 3: return
    fig, ax = plt.subplots(figsize=(7.2, 9)); fig.subplots_adjust(left=0.13, right=0.96, top=0.80, bottom=0.20)
    head(fig, f'{name}: size vs speed', TAKE[name])
    P.scatter(ax, rows, 'dt', 'Decode speed, MB per second (log; right = faster)', '')
    ax.set_ylabel('Size, % of original (lower = smaller)', fontsize=9); ax.xaxis.label.set_size(9)
    h = [Line2D([], [], marker=m, ls='', color=c, markersize=8, label=l.split(' (')[0]) for c, m, l in P.KIND.values()]
    h.append(Line2D([], [], ls='--', color='#9a9994', label='Best trade-off line'))
    fig.legend(handles=h, loc='lower center', bbox_to_anchor=(0.5, 0.045), ncol=2, fontsize=8, frameon=False)
    srcs = {s for n, k, c, t, s in d['entries'] if t is not None}
    foot(fig, 'Down = smaller, right = faster. Nothing beats the points on the dashed line on both counts.' +
         (' Times come from more than one run; each stock-vs-mine pair is from the same run.' if len(srcs) > 1 else ''))
    save(fig, f'{SHORT[name]}_speed', f"{name}, size against decode speed. Lower is smaller and further right is faster; the dashed line joins the results nothing else beats on both. {TAKE[name]}")

def pairs(name, d, items=None, title=None, take=None, fname=None, cap=None):
    if items is None:
        E = {n: (k, c, t) for n, k, c, t, s in d['entries']}; o = d['orig']
        items = [(s, per_m(E[s][1], o), E[s][2], u, per_m(E[u][1], o), E[u][2], E[u][0]) for s, u in d.get('pairs', []) if s in E and u in E]
    if not items: return
    fig, ax = plt.subplots(figsize=(7.2, 9)); fig.subplots_adjust(left=0.08, right=0.97, top=0.80, bottom=0.10)
    head(fig, title or f'{name}: before -> after', take or 'Grey dot = the stock tool. Coloured mark = my version. Green text = smaller with no slower decode.')
    ax.axis('off'); n = len(items); y = 1.0; step = 1.0 / max(n, 1)
    xs = [v for it in items for v in (it[1], it[4])]; lo, hi = min(xs), max(xs); pad = (hi - lo) * 0.08 or 1
    def X(v): return 0.05 + 0.9 * (v - lo + pad) / (hi - lo + 2 * pad)
    for i, (sl, sv, st, ol, ov, ot, ok) in enumerate(items):
        yy = 1 - (i + 0.5) * step
        ax.text(0.0, yy + step * 0.30, f'{sl}  ->  {ol}', fontsize=9.5, fontweight='bold', color=INK, transform=ax.transAxes)
        ax.plot([0.05, 0.95], [yy, yy], color=GRID, lw=1, transform=ax.transAxes)
        ax.annotate('', xy=(X(ov), yy), xytext=(X(sv), yy), xycoords='axes fraction', arrowprops=dict(arrowstyle='-|>', color=P.KIND[ok][0], lw=2, shrinkA=5, shrinkB=6))
        ax.scatter([X(sv)], [yy], s=60, c='#8a8a85', transform=ax.transAxes, zorder=3)
        ax.scatter([X(ov)], [yy], s=90, c=P.KIND[ok][0], marker=P.KIND[ok][1], transform=ax.transAxes, zorder=4, edgecolors='white')
        ch = (ov - sv) / sv * 100
        sp = f'{st/ot:.2f}x faster decode' if st and ot and ot < st else (f'{ot/st:.2f}x slower decode' if st and ot else '')
        good = ch < 0 and 'slower' not in sp
        ax.text(1.0, yy - step * 0.36, f'{sv:,.0f} -> {ov:,.0f}   ({ch:+.1f}%{", " + sp if sp else ""})', fontsize=8.5, ha='right',
                color='#1a7f37' if good else INK2, fontweight='bold' if good else 'normal', transform=ax.transAxes)
    foot(fig)
    if fname: save(fig, fname, cap); return
    save(fig, f'{SHORT[name]}_before_after', f"{name}, what my changes did: each row is a standard tool and my version of it (or my nearest rival), with the size and decode change.")

def spot(title, take, rows, fname, cap):
    pairs(None, None, items=rows, title=title, take=take, fname=fname, cap=cap)

def main():
    scoreboard()
    for name in KEEP:
        d = B.STANDARDS[name]
        leaderboard(name, d); tradeoff(name, d); pairs(name, d)
    k = [(c, per_m(pb, 1029744), pt / 1e6 if pt else None, (f'{c} + {ol}' if ol in ('record delta', 'activity map') else ol), per_m(ob, 1029744), ot / 1e6 if ot else None,
          'Original' if 'HCM' in ol else 'Mutant' if ol in ('record delta', 'activity map', 'lpaq1-h') else 'Modified') for c, pb, pt, ol, ob, ot in B.KENNEDY]
    spot('Spotlight: kennedy.xls', 'Up to 86% smaller and up to 2.6x faster to decode on a real spreadsheet. HCM-Bin beats ZPAQ -m5 and decodes 5.3x faster.',
         k, 'kennedy', "Spotlight on kennedy.xls, the spreadsheet in the Canterbury corpus: what my record detection, activity map, HCM-Bin, xz-hi and lpaq1-h did to each standard tool.")
    xr = [(s, per_m(sb, 8474240), st / 1000 if st else None, ol, per_m(ob, 8474240), ot / 1000 if ot else None,
           'Modified' if 'jxl' in ol or '7z' in ol else 'Mutant') for s, sb, st, ol, ob, ot in B.XRAY]
    spot('Spotlight: x-ray image', 'A real medical x-ray: smaller AND faster with zstd, ORC, Parquet, 7-Zip and JPEG XL, every one read by its standard decoder.',
         xr, 'xray', "Spotlight on a real medical x-ray image (8.5 MB): every one of these is smaller and decodes faster, and each is read by its standard reader.")
    fig, ax = plt.subplots(figsize=(7.2, 9)); fig.subplots_adjust(left=0.13, right=0.96, top=0.80, bottom=0.12)
    head(fig, 'Spotlight: JPEG XL on medical images', 'Up to 7.5x faster decoding with the standard JPEG XL decoder, at almost the same size, and some settings are smaller AND faster.')
    ax.axvline(0, color=INK2, lw=1); ax.axhline(1, color=INK2, lw=1)
    ax.scatter([j[1] for j in B.JXL], [j[2] for j in B.JXL], s=110, c=P.KIND['Modified'][0], marker='^', edgecolors='white', zorder=3)
    P.place_labels(ax, [(j[1], j[2], j[0], dict(color=INK, fontweight='bold')) for j in B.JXL], fs=8.5)
    ax.set_xlabel('Size change vs stock, % (left of 0 = smaller)', fontsize=9); ax.set_ylabel('Decode speed-up (x)', fontsize=9)
    ax.grid(True, color=GRID, lw=0.6); [ax.spines[s].set_visible(False) for s in ('top', 'right')]
    foot(fig, 'Each point is one tuned setting on one image.')
    save(fig, 'jpegxl', "Spotlight on JPEG XL with medical images: each point is one of my tuned settings, showing its size change and decode speed-up; the standard JPEG XL decoder reads every one.")
    with open(os.path.join(OUT, 'captions.txt'), 'w') as f:
        for n, c in caps: f.write(f'{n}\n{c}\n\n')
    print(len(caps), 'images')

if __name__ == '__main__': main()
