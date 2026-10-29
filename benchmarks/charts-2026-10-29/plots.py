#!/usr/bin/env python3
"""Benchmark chart assembly: standard tools vs Harrington originals, modifieds and mutants.
Reads results.json (bench.py) and extra_sets.json (earlier biased/radio runs).
Writes PNG charts to charts/ and a combined PDF report Harrington-Benchmark-Charts.pdf."""
import json, os, sys, textwrap
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.lines import Line2D

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'charts'); os.makedirs(OUT, exist_ok=True)
R = json.load(open(os.path.join(HERE, 'results.json')))
X = json.load(open(os.path.join(HERE, 'extra_sets.json')))

KIND = {  # colour + marker: identity is never colour alone
 'Standard': ('#8a8a85', 'o', 'Standard tool'),
 'Original': ('#2a78d6', 's', 'Harrington original (own design)'),
 'Modified': ('#1baf7a', '^', 'Harrington modified (stock decoder reads it)'),
 'Mutant':   ('#eb6834', 'D', 'Harrington mutant (changed decoder)'),
}
OURS = ('Original', 'Modified', 'Mutant')
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 9, 'axes.edgecolor': '#b5b4ae',
    'axes.labelcolor': INK2, 'xtick.color': INK2, 'ytick.color': INK2, 'axes.titlesize': 11,
    'axes.titleweight': 'bold', 'axes.titlecolor': INK, 'figure.facecolor': 'white'})

def totals():
    sets = {}
    for k, v in R.items():
        f = v['files'].values()
        o = sum(x['orig'] for x in f); c = sum(x['comp'] for x in f)
        ct = sum(x['ct'] for x in f); dt = sum(x['dt'] for x in f)
        ram = max(max(x.get('cram', 0), x.get('dram', 0)) for x in f)
        sets.setdefault(v['set'], {})[v['tool']] = dict(kind=v['kind'], orig=o, comp=c, ct=ct, dt=dt,
            ram=ram, files={n: x['comp'] / x['orig'] * 100 for n, x in v['files'].items()})
    # Silesia sample and Synthetic: existing recorded data only (no new runs)
    G = json.load(open(os.path.join(HERE, 'goodwin_corpora_cloud.json')))['res']
    L = json.load(open(os.path.join(HERE, 'lpaq1h_goodwin_sets.json')))
    keep = {'lz4 -1': 'lz4 -1', 'gzip -9': 'gzip -9', 'bzip2 -9': 'bzip2 -9', 'zstd -3': 'zstd -3', 'zstd -19': 'zstd -19',
            'xz -9e': 'xz -9e', 'lpaq1 6': 'lpaq1 -6', 'ZPAQ -m5': 'ZPAQ -m5', 'HCM fast': 'HCM fast*', 'HCM max': 'HCM max*'}
    for gname, sname in (('Silesia (first 1 MB of each file)', 'Silesia 1 MB sample'), ('Synthetic', 'Synthetic set')):
        rows = {}
        for k, n in keep.items():
            v = G[gname][k]
            rows[n] = dict(kind='Original' if k.startswith('HCM') else 'Standard', orig=v['orig'], comp=v['comp'],
                           ct=v['ct'], dt=v['dt'], ram=max(v['cram'], v['dram']), files={})
        t = L[gname]['total']
        rows['lpaq1-h*'] = dict(kind='Mutant', orig=t['orig'], comp=t['comp'], ct=t['ct'], dt=t['dt'], ram=None, files={})
        if sname.startswith('Silesia') and 'Silesia 1 MB sample|bzip2-hi' in R:
            f = R['Silesia 1 MB sample|bzip2-hi']['files'].values()
            rows['bzip2-hi*'] = dict(kind='Modified', orig=sum(x['orig'] for x in f), comp=sum(x['comp'] for x in f),
                                    ct=sum(x['ct'] for x in f), dt=sum(x['dt'] for x in f), ram=None, files={})
        sets[sname] = rows
    sets = {k: v for k, v in sets.items() if k == 'Canterbury' or '*' in ''.join(v)}
    return sets

def pareto(pts):
    """pts: list of (speed, pct, name). Frontier = nothing both faster and smaller."""
    fr = [p for p in pts if not any(q[0] >= p[0] and q[1] <= p[1] and (q[0] > p[0] or q[1] < p[1]) for q in pts)]
    return sorted(fr)

def scatter(ax, rows, speed_key, xlabel, title):
    pts = []; lbl = []
    for name, r in rows.items():
        t = r.get(speed_key)
        if not t: continue
        pts.append((r['orig'] / 1e6 / t, r['comp'] / r['orig'] * 100, name))
    fr = pareto(pts); frn = {p[2] for p in fr}
    ax.plot([p[0] for p in fr], [p[1] for p in fr], ls='--', lw=1.2, color='#9a9994', zorder=1)
    for s, pct, name in pts:
        col, m, _ = KIND[rows[name]['kind']]
        ours = rows[name]['kind'] in OURS
        ax.scatter(s, pct, s=70 if ours else 46, c=col, marker=m, zorder=3,
                   edgecolors=INK if name in frn else 'white', linewidths=1.4 if name in frn else 1.0)
        lbl.append((s, pct, name, ours))
    ax.set_xscale('log')
    xs = [p[0] for p in pts]; lo, hi = min(xs), max(xs)
    ax.set_xlim(lo / 2.5, hi * 2.5)
    lbl.sort(key=lambda t: t[1]); placed = []
    import math
    ylo, yhi = min(p[1] for p in pts), max(p[1] for p in pts); yr = (yhi - ylo) or 1
    for s, pct, name, ours in lbl:
        right = s > hi / 6   # near the right edge: label goes on the left
        dy = 3
        while any(abs(math.log10(s) - a) < 0.6 and abs((pct - b) / yr * 380 + dy - c) < 10 for a, b, c in placed):
            dy += 10
        placed.append((math.log10(s), pct, dy))
        ax.annotate(name, (s, pct), xytext=(-7 if right else 7, dy), textcoords='offset points', fontsize=7.5,
                    ha='right' if right else 'left', color=INK if ours else INK2, fontweight='bold' if ours else 'normal')
    ax.set_xscale('log'); ax.set_xlabel(xlabel); ax.set_ylabel('Compressed size, % of original (lower is better)')
    ax.set_title(title, loc='left'); ax.grid(True, color=GRID, lw=0.6); ax.set_axisbelow(True)
    for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
    return fr

def legend(fig, extra_note=None):
    h = [Line2D([], [], marker=m, ls='', color=c, markersize=8, markeredgecolor='white', label=l) for c, m, l in KIND.values()]
    h.append(Line2D([], [], ls='--', color='#9a9994', label='Best-trade-off line (nothing beats these on both)'))
    h.append(Line2D([], [], marker='o', ls='', color='white', markeredgecolor=INK, markersize=8, label='On the line'))
    fig.legend(handles=h, loc='lower center', ncol=3, frameon=False, fontsize=8)

def save(fig, name, pdf):
    fig.savefig(os.path.join(OUT, name), dpi=160); pdf.savefig(fig); plt.close(fig)

def text_page(pdf, title, body, name=None):
    fig = plt.figure(figsize=(8.5, 11)); fig.text(0.08, 0.93, title, fontsize=17, fontweight='bold', color=INK)
    y = 0.89
    for para in body:
        for line in textwrap.wrap(para, 95) or ['']:
            fig.text(0.08, y, line, fontsize=9.5, color=INK, family='DejaVu Sans'); y -= 0.017
        y -= 0.010
    if name: fig.savefig(os.path.join(OUT, name), dpi=130)
    pdf.savefig(fig); plt.close(fig)

def summary(sets):
    lines = []
    for sname, rows in sets.items():
        def best(filter_, key):
            c = [(r[key], n) for n, r in rows.items() if filter_(r) and r.get(key)]
            return min(c) if c else (None, None)
        sz, sn = best(lambda r: True, 'comp'); so, son = best(lambda r: r['kind'] in OURS, 'comp')
        ss, ssn = best(lambda r: r['kind'] == 'Standard', 'comp')
        orig = next(iter(rows.values()))['orig']
        L = f"{sname} ({orig:,} bytes, {len(rows)} tools). Smallest overall: {sn} at {sz:,} bytes. Best standard: {ssn} at {ss:,}."
        if son: L += f" Our best: {son} at {so:,} bytes ({(so-ss)/ss*100:+.2f}% vs best standard)."
        pts = [(r['orig'] / 1e6 / r['dt'], r['comp'] / r['orig'] * 100, n) for n, r in rows.items() if r.get('dt')]
        ours_fr = [p[2] for p in pareto(pts) if rows[p[2]]['kind'] in OURS]
        L += (" Ours on the decode-speed trade-off line: " + ', '.join(ours_fr) + '.') if ours_fr else " None of ours sits on the decode-speed trade-off line."
        # wins on both: our tool smaller AND faster-decoding than a standard tool
        both = []
        for n, r in rows.items():
            if r['kind'] not in OURS or not r.get('dt'): continue
            beat = [m for m, s in rows.items() if s['kind'] == 'Standard' and s.get('dt') and r['comp'] < s['comp'] and r['dt'] < s['dt']]
            if beat: both.append(f"{n} beats {', '.join(beat)}")
        if both: L += " Smaller AND faster to decode: " + '; '.join(both) + '.'
        lines.append(L)
    return lines

def main():
    sets = totals()
    pdf = PdfPages(os.path.join(HERE, 'Harrington-Benchmark-Charts.pdf'))
    intro = [
     "Harrington's Insights: benchmark charts, standard compressors against the author's originals, modified tools and mutants. Assembled 29 October 2026 from recorded results; one run per tool per file, and every result was decompressed and checked byte for byte when it was recorded.",
     "Classes. Standard: the stock tool. Original: the author's own designs (HCM, hcmx, Harrington Exchange). Modified: a tuned encoder whose output the stock decoder reads unchanged (bzip2-hi, xz-hi). Mutant: an existing compressor whose decoder was changed (lpaq1-h, the Exchange->Blosc2 hybrid, Exchange->xz).",
     "Sets follow Andrew Goodwin's review: the Canterbury corpus, the first 1 MB of each Silesia file, and his synthetic set; plus the author's biased packed-data set and six radio captures from earlier runs.",
     "How to read the dot charts. Each dot is one tool's total over a set. Up and down is size (lower is smaller); left to right is speed in MB of original data per second on a log scale (further right is faster). The dashed line joins the tools that nothing else beats on both size and speed; dots with a dark ring sit on it. Our tools are drawn larger with bold labels. The line charts show each file separately.",
     "Data sources. Canterbury: one fresh run of every tool on one machine (29 Oct 2026). Silesia sample and Synthetic set: no new runs; the standard tools and HCM (marked *, Goodwin's HCM build) are Andrew Goodwin's recorded results (single 2.1 GHz Xeon core), and lpaq1-h* and bzip2-hi* are the author's recorded runs on a different machine, so their times are comparable only roughly (lpaq1-h decoded at the same speed as stock lpaq1 on the author's machine). Peak memory and file-by-file charts are given for Canterbury only, where every tool was measured together.",
     "Timing notes. Times are wall clock including program start-up, so on small files the fast tools all look about the same; sizes are exact. lpaq1-h's compression time is the sum of its six tries (it keeps the best). ZPAQ sizes have its 1,015-byte per-file archive header removed, as in Goodwin's review. On the biased and radio sets decode times are earlier in-process measurements and compression times were mostly not recorded, so those two sets have decode charts only; ZPAQ's radio decode time is an estimate.",
     "RESULTS"] + summary(sets)
    text_page(pdf, 'Benchmark Charts', intro, 'summary.png')
    for sname, rows in sets.items():
        fig, axs = plt.subplots(1, 2, figsize=(14, 6.6))
        scatter(axs[0], rows, 'dt', 'Decompression speed, MB/s (log)', f'{sname}: size vs decode speed')
        scatter(axs[1], rows, 'ct', 'Compression speed, MB/s (log)', f'{sname}: size vs compression speed')
        legend(fig); fig.subplots_adjust(bottom=0.2, left=0.06, right=0.98, wspace=0.18)
        save(fig, f'{sname.split()[0].lower()}_scatter.png', pdf)
        if any(r['ram'] is None for r in rows.values()): continue
        # RAM chart
        fig, ax = plt.subplots(figsize=(8.5, 5.5))
        for n, r in rows.items():
            col, m, _ = KIND[r['kind']]
            ax.scatter(r['ram'], r['comp'] / r['orig'] * 100, c=col, marker=m, s=60, edgecolors='white')
            ax.annotate(n, (r['ram'], r['comp'] / r['orig'] * 100), xytext=(5, 3), textcoords='offset points', fontsize=7.5,
                        color=INK if r['kind'] in OURS else INK2, fontweight='bold' if r['kind'] in OURS else 'normal')
        ax.set_xscale('log'); ax.set_xlabel('Peak memory, MB (log; lower is lighter)'); ax.set_ylabel('Size, % of original')
        ax.set_title(f'{sname}: size vs peak memory', loc='left'); ax.grid(True, color=GRID, lw=0.6)
        for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
        legend(fig); fig.subplots_adjust(bottom=0.25); save(fig, f'{sname.split()[0].lower()}_ram.png', pdf)
        # Line chart per file: strongest standard tools vs our best
        std = sorted([n for n, r in rows.items() if r['kind'] == 'Standard'], key=lambda n: rows[n]['comp'])[:3]
        ours = sorted([n for n, r in rows.items() if r['kind'] in OURS], key=lambda n: rows[n]['comp'])[:3]
        pick = std + ours; files = list(rows[pick[0]]['files'])
        fig, ax = plt.subplots(figsize=(12, 6))
        greys = ['#5c5b57', '#8a8a85', '#b5b4ae']; cols = ['#2a78d6', '#eb6834', '#1baf7a']
        for i, n in enumerate(pick):
            r = rows[n]; c = greys[i] if i < len(std) else cols[i - len(std)]
            y = [r['files'].get(f) for f in files]
            ax.plot(range(len(files)), y, marker=KIND[r['kind']][1], color=c, lw=2, ms=6,
                    ls='--' if r['kind'] == 'Standard' else '-', label=f"{n} ({r['kind']}) - total {r['comp']:,} B")
        ax.set_xticks(range(len(files))); ax.set_xticklabels(files, rotation=40, ha='right', fontsize=7.5)
        ax.set_ylabel('Size, % of original (lower is better)')
        ax.set_title(f'{sname}: file by file, three strongest standard tools (dashed) vs our three best (solid)', loc='left')
        ax.grid(True, color=GRID, lw=0.6); ax.legend(frameon=False, fontsize=8)
        for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
        fig.subplots_adjust(bottom=0.25, left=0.07, right=0.98); save(fig, f'{sname.split()[0].lower()}_perfile.png', pdf)
        # bytes vs ZPAQ -m5 per file
        if 'ZPAQ -m5' in rows:
            z = rows['ZPAQ -m5']; fig, ax = plt.subplots(figsize=(12, 5.5))
            ax.axhline(0, color=INK2, lw=1)
            for i, n in enumerate(ours):
                r = rows[n]; y = [(r['files'][f] - z['files'][f]) / z['files'][f] * 100 for f in files]
                ax.plot(range(len(files)), y, marker=KIND[r['kind']][1], color=cols[i], lw=2, ms=6, label=n)
            ax.set_xticks(range(len(files))); ax.set_xticklabels(files, rotation=40, ha='right', fontsize=7.5)
            ax.set_ylabel('Size difference vs ZPAQ -m5, % (below 0 = ours smaller)')
            ax.set_title(f'{sname}: our three best against ZPAQ -m5, file by file', loc='left')
            ax.grid(True, color=GRID, lw=0.6); ax.legend(frameon=False, fontsize=8)
            for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
            fig.subplots_adjust(bottom=0.25, left=0.08, right=0.98); save(fig, f'{sname.split()[0].lower()}_vs_zpaq.png', pdf)
    # earlier sets: decode only
    for sname, d in X.items():
        if sname.startswith('_'): continue
        rows = {}
        for n, t in d['tools'].items():
            rows[n] = dict(kind=t['kind'], orig=d['orig'], comp=t['comp'], dt=(t['dt_ms'] / 1000 if t.get('dt_ms') else None))
        missing = [n for n, r in rows.items() if not r['dt']]
        fig, ax = plt.subplots(figsize=(11, 6.6))
        scatter(ax, rows, 'dt', 'Decompression speed, MB/s (log)', f'{sname}: size vs decode speed')
        note = 'Compression times were not recorded for this set; decode times are earlier in-process measurements.'
        if missing:
            note += ' Size only (no decode time recorded): ' + '; '.join(f"{n} {rows[n]['comp']/rows[n]['orig']*100:.1f}%" for n in sorted(missing, key=lambda n: rows[n]['comp'])) + '.'
        fig.text(0.06, 0.15, textwrap.fill(note, 170), fontsize=8, color=INK2, va='top')
        legend(fig); fig.subplots_adjust(bottom=0.24, left=0.07, right=0.98)
        save(fig, f"{sname.split()[0].lower()}_scatter.png", pdf)
    pdf.close(); print('ok', sorted(os.listdir(OUT)))

if __name__ == '__main__': main()
