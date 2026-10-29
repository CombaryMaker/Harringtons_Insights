#!/usr/bin/env python3
"""Best-of-the-best assembly: every recorded result per standard benchmark, ours vs stock.
Data: best_data.py (book tables + same-machine run). Output: Harrington-Best-Results.pdf and charts/best_*.png.
The detailed Goodwin-set charts (Harrington-Benchmark-Charts.pdf, from plots.py) are appended at the end."""
import os, textwrap
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.lines import Line2D
import plots as P          # shared styling, label placer, trade-off scatter
import best_data as B

P.KIND['Best-of'] = ('#6250d6', 'P', 'Harrington best-of (best of our tools, picked per file)')
P.OURS = ('Original', 'Modified', 'Mutant', 'Best-of')
OURS = P.OURS; INK, INK2, GRID = P.INK, P.INK2, P.GRID
OUT = P.OUT
def per_m(c, o): return c / o * 1e6
def fmt_t(t):
    if t is None: return 'decode not recorded'
    if t < 0.01: return f'{t*1000:.2f} ms'
    return f'{t*1000:.0f} ms' if t < 1 else f'{t:.1f} s'
def clean(ax):
    for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
    ax.grid(True, axis='x', color=GRID, lw=0.6); ax.set_axisbelow(True)

def best_lines(name, d):
    E = d['entries']; o = d['orig']
    ours = [x for x in E if x[1] in OURS]; std = [x for x in E if x[1] == 'Standard' and x[0] != 'Best standard per file']
    bo = min(ours, key=lambda x: x[2]); bs = min(std, key=lambda x: x[2])
    timed = [x for x in std if x[3] is not None]
    # our smallest entry that is also faster to decode than some standard tool it beats on size
    wins = []
    for x in sorted(ours, key=lambda x: x[2]):
        if x[3] is None: continue
        beat = [s for s in timed if x[2] < s[2] and x[3] < s[3]]
        if beat:
            top = min(beat, key=lambda s: s[2]); wins.append((x, top))
    return o, bo, bs, wins

def scoreboard(pdf):
    rows = []
    for name, d in B.STANDARDS.items():
        o, bo, bs, wins = best_lines(name, d)
        verdict = 'OURS SMALLEST\nOF ALL' if bo[2] < bs[2] else f'{(bo[2]-bs[2])/bs[2]*100:.1f}% behind\n{bs[0]}'
        w = wins[0] if wins else None
        rows.append([textwrap.fill(name, 22), f"{bo[0]}\n{per_m(bo[2], o):,.0f}  ({fmt_t(bo[3])})", f"{bs[0]}\n{per_m(bs[2], o):,.0f}  ({fmt_t(bs[3])})",
                     verdict, (f"{w[0][0]} {per_m(w[0][2], o):,.0f} in {fmt_t(w[0][3])}\nbeats {w[1][0]} {per_m(w[1][2], o):,.0f} in {fmt_t(w[1][3])}") if w else '-'])
    fig = plt.figure(figsize=(11, 8.5)); fig.text(0.04, 0.95, 'Scoreboard: our best against the best standard tool, every benchmark', fontsize=15, fontweight='bold', color=INK)
    fig.text(0.04, 0.915, 'Sizes are 1,000,000 bits down to X bits. "Smaller AND faster" is our smallest entry that beats a standard tool on both size and decode time.', fontsize=8.5, color=INK2)
    ax = fig.add_axes([0.02, 0.04, 0.96, 0.85]); ax.axis('off')
    t = ax.table(cellText=rows, colLabels=['Benchmark', 'Our best (size, decode)', 'Best standard tool (size, decode)', 'Size verdict', 'Smaller AND faster'],
                 colWidths=[0.15, 0.22, 0.21, 0.15, 0.27], loc='upper center', cellLoc='left')
    t.auto_set_font_size(False); t.set_fontsize(7.2)
    for (r, c), cell in t.get_celld().items():
        cell.set_edgecolor('#d8d7d2'); cell.set_height(0.072 if r else 0.04)
        if r == 0: cell.set_facecolor('#efeeea'); cell.set_text_props(fontweight='bold')
        elif c == 3 and 'OURS' in rows[r - 1][3]: cell.set_text_props(color='#1a7f37', fontweight='bold')
    fig.savefig(os.path.join(OUT, 'best_scoreboard.png'), dpi=150); pdf.savefig(fig); plt.close(fig)

def leaderboard(pdf, name, d, slug):
    E = sorted(d['entries'], key=lambda x: x[2]); o = d['orig']
    h = max(4.5, 0.32 * len(E) + 1.8)
    fig, ax = plt.subplots(figsize=(11, h)); fig.subplots_adjust(left=0.30, right=0.84, top=1 - 0.7 / h, bottom=1.45 / h)
    y = range(len(E))[::-1]
    mixed = 'run' in {s for n, k, c, t, s in E if t is not None} and len({s for n, k, c, t, s in E if t is not None}) > 1
    for yi, (n, k, c, t, src) in zip(y, E):
        col = P.KIND[k][0]; ours = k in OURS
        ax.barh(yi, per_m(c, o), color=col, height=0.62, edgecolor='white', linewidth=1)
        ax.text(per_m(c, o), yi, f'  {per_m(c, o):,.0f}   ·  {fmt_t(t)}{" †" if mixed and src != "run" and t is not None else ""}', va='center', fontsize=7.5,
                color=INK if ours else INK2, fontweight='bold' if ours else 'normal')
    ax.set_yticks(list(y)); ax.set_yticklabels([x[0] for x in E], fontsize=8)
    for lbl, x in zip(ax.get_yticklabels(), E):
        if x[1] in OURS: lbl.set_fontweight('bold'); lbl.set_color(INK)
        else: lbl.set_color(INK2)
    lo = min(per_m(x[2], o) for x in E); hi = max(per_m(x[2], o) for x in E)
    ax.set_xlim(max(0, lo - (hi - lo) * 0.15) if hi / lo < 1.6 else 0, hi * 1.02)
    ax.set_xlabel('1,000,000 bits down to ... bits (shorter bar is better)')
    ax.set_title(f'{name}: every recorded result, smallest first  ({o:,} bytes)', loc='left'); clean(ax)
    h_ = [Line2D([], [], marker='s', ls='', color=c, markersize=9, label=l) for k, (c, m, l) in P.KIND.items()]
    fig.legend(handles=h_, loc='lower center', bbox_to_anchor=(0.5, 0.42 / h), ncol=3, fontsize=7, frameon=False)
    srcs = {s for n, k, c, t, s in E if t is not None}
    foot = ('Decode times: † = recorded in a book chapter run (in-process); unmarked = 29 Oct 2026 same-machine run (wall clock). ' if 'run' in srcs and len(srcs) > 1 else '')
    if d.get('time_label'): foot += 'Time shown is ' + d['time_label'].lower() + '. '
    if d.get('note'): foot += 'Note: ' + d['note']
    if foot: fig.text(0.02, 0.12 / h, textwrap.fill(foot, 200), fontsize=7.5, color=INK2)
    fig.savefig(os.path.join(OUT, f'best_{slug}_leaderboard.png'), dpi=150); pdf.savefig(fig); plt.close(fig)

def tradeoff(pdf, name, d, slug):
    rows = {n: dict(kind=k, orig=d['orig'], comp=c, dt=t) for n, k, c, t, s in d['entries'] if t is not None}
    if len(rows) < 3: return
    fig, ax = plt.subplots(figsize=(11, 7.2)); fig.subplots_adjust(bottom=0.25, left=0.08, right=0.98)
    P.scatter(ax, rows, 'dt', 'Decode speed, MB of original per second (log; right is faster)' if not d.get('time_label') else
              'Speed, MB of original per second of ' + d['time_label'].lower(), f'{name}: size vs decode speed, all timed results')
    ax.set_ylabel('Size, % of original (lower is better)')
    srcs = sorted({s for n, k, c, t, s in d['entries'] if t is not None})
    fig.text(0.06, 0.125, textwrap.fill('Timing sources: ' + ', '.join(srcs) + ('. Times from different book chapters were measured in different runs; each stock-vs-ours pair comes from the same run.' if len(srcs) > 1 else '.'), 170), fontsize=7.5, color=INK2)
    P.legend(fig); fig.savefig(os.path.join(OUT, f'best_{slug}_tradeoff.png'), dpi=150); pdf.savefig(fig); plt.close(fig)

def dumbbell(pdf, title, items, unit, fname, logx=False, note=None):
    """items: (stock label, stock value, stock time, ours label, ours value, ours time, ours kind)"""
    h = max(3.6, 0.55 * len(items) + 1.8)
    fig, ax = plt.subplots(figsize=(11, h)); fig.subplots_adjust(left=0.27, right=0.70, top=1 - 0.8 / h, bottom=1.0 / h)
    for i, (sl, sv, st, ol, ov, ot, ok) in enumerate(items[::-1]):
        col = P.KIND[ok][0]
        ax.annotate('', xy=(ov, i), xytext=(sv, i), arrowprops=dict(arrowstyle='-|>', color=col, lw=1.8, shrinkA=5, shrinkB=5))
        ax.scatter([sv], [i], s=55, c='#8a8a85', zorder=3, edgecolors='white')
        ax.scatter([ov], [i], s=80, c=col, marker=P.KIND[ok][1], zorder=4, edgecolors='white')
        sp = f'{st/ot:.2f}x faster decode' if st and ot and ot < st else (f'{ot/st:.2f}x slower decode' if st and ot else 'decode no slower' if ok == 'Modified' else '')
        ch = (ov - sv) / sv * 100
        ax.text(1.02, i, f'{ch:+.1f}% size   {sp}', transform=ax.get_yaxis_transform(), va='center', fontsize=8,
                color='#1a7f37' if ch < 0 and 'slower' not in sp else INK2, fontweight='bold' if ch < 0 and 'slower' not in sp else 'normal')
    ax.set_yticks(range(len(items))); ax.set_yticklabels([f'{it[0]}  ->  {it[3]}' for it in items[::-1]], fontsize=8)
    if logx: ax.set_xscale('log')
    ax.set_xlabel(unit); ax.set_title(title, loc='left'); clean(ax)
    if note: fig.text(0.02, 0.25 / h, textwrap.fill(note, 190), fontsize=7.5, color=INK2)
    fig.savefig(os.path.join(OUT, fname), dpi=150); pdf.savefig(fig); plt.close(fig)

def pairs_for(d):
    E = {n: (k, c, t) for n, k, c, t, s in d['entries']}; o = d['orig']; out = []
    for s, u in d.get('pairs', []):
        if s in E and u in E:
            out.append((s, per_m(E[s][1], o), E[s][2], u, per_m(E[u][1], o), E[u][2], E[u][0]))
    return out

def main():
    pdf = PdfPages(os.path.join(P.HERE, 'Harrington-Best-Results.pdf'))
    P.text_page(pdf, "Harrington's Insights: Best Results", [
        "This assembly puts the best recorded result of every tool the author built or modified, originals (HCM, hcmx, the Harrington Exchange), modified encoders read by stock decoders (xz-hi, lzma-hi, 7z-hi, zip-hi, bzip2-hi, zstd-hi, Brotli-hi, flac-hi, the PDF row filter, RAR with detector delta) and mutants (record delta front ends, lpaq1-h, the Exchange->Blosc2 hybrid), beside every standard tool measured on the same benchmark.",
        "Sizes are shown as 1,000,000 bits down to X bits. 'Harrington best-of' is the smallest of our own tools picked file by file, the same way a production system would pick per file; 'Best standard per file' does the same for the stock tools, so the comparison is fair both ways.",
        "No new measurements were made for this assembly. Results come from the book's recorded tables (Chapters 4, 5, 6, 13, 15-18 and Appendix A) and from the 29 October 2026 same-machine run of Goodwin's three sets. Entries marked 'decode not recorded' appear in the size rankings only.",
        "Order: a scoreboard, then for each benchmark a full ranking, a size-versus-decode-speed chart and a stock-versus-ours chart for each tool we modified; then spotlights on kennedy.xls, the x-ray image and JPEG XL; then the detailed Goodwin-set charts.",
        "Results that are not exceptional are listed here for completeness: " + ' '.join(B.MINOR)], 'best_cover.png')
    scoreboard(pdf)
    for name, d in B.STANDARDS.items():
        slug = ''.join(ch for ch in name.lower().split()[0] if ch.isalnum()) + ('_full' if 'full' in name else '_1mb' if '1 MB' in name else '')
        leaderboard(pdf, name, d, slug)
        tradeoff(pdf, name, d, slug)
        pr = pairs_for(d)
        if pr: dumbbell(pdf, f'{name}: what our changes did to each tool', pr, '1,000,000 bits down to ... bits (arrow points to ours)', f'best_{slug}_pairs.png')
    k = [(c, per_m(pb, 1029744), pt / 1e6 if pt else None, (f'{c} + {ol}' if ol in ('record delta', 'activity map') else ol), per_m(ob, 1029744), ot / 1e6 if ot else None,
          'Original' if 'HCM' in ol else 'Mutant' if ol in ('record delta', 'activity map', 'lpaq1-h') else 'Modified') for c, pb, pt, ol, ob, ot in B.KENNEDY]
    dumbbell(pdf, 'Spotlight: kennedy.xls (spreadsheet, 1,029,744 bytes)', k, '1,000,000 bits down to ... bits (log scale)', 'best_kennedy.png', logx=True)
    xr = [(s, per_m(sb, 8474240), st / 1000 if st else None, ol, per_m(ob, 8474240), ot / 1000 if ot else None,
           'Modified' if 'jxl' in ol or '7z' in ol else 'Mutant') for s, sb, st, ol, ob, ot in B.XRAY]
    dumbbell(pdf, 'Spotlight: x-ray medical image (8,474,240 bytes)', xr, '1,000,000 bits down to ... bits', 'best_xray.png')
    fig, ax = plt.subplots(figsize=(11, 6.5)); fig.subplots_adjust(bottom=0.12, left=0.08, right=0.97)
    ax.axvline(0, color=INK2, lw=1); ax.axhline(1, color=INK2, lw=1)
    ax.scatter([j[1] for j in B.JXL], [j[2] for j in B.JXL], s=90, c=P.KIND['Modified'][0], marker='^', edgecolors='white', zorder=3)
    P.place_labels(ax, [(j[1], j[2], f'{j[0]} ({j[3]})', dict(color=INK, fontweight='bold')) for j in B.JXL], fs=8)
    ax.set_xlabel('Size change vs stock cjxl effort 7, % (left of 0 = smaller)'); ax.set_ylabel('Decode speed-up vs stock (x)')
    ax.set_title('Spotlight: jxl-hi on medical images (stock JPEG XL decoder reads every file)', loc='left')
    ax.grid(True, color=GRID, lw=0.6); [ax.spines[s].set_visible(False) for s in ('top', 'right')]
    fig.savefig(os.path.join(OUT, 'best_jxl.png'), dpi=150); pdf.savefig(fig); plt.close(fig)
    pdf.close()
    # append the detailed Goodwin-set charts
    from pypdf import PdfReader, PdfWriter
    w = PdfWriter()
    for f in ('Harrington-Best-Results.pdf', 'Harrington-Benchmark-Charts.pdf'):
        for pg in PdfReader(os.path.join(P.HERE, f)).pages: w.add_page(pg)
    with open(os.path.join(P.HERE, 'Harrington-Best-Results-Full.pdf'), 'wb') as fh: w.write(fh)
    print('ok', len(PdfReader(os.path.join(P.HERE, 'Harrington-Best-Results-Full.pdf')).pages), 'pages')

if __name__ == '__main__': main()
