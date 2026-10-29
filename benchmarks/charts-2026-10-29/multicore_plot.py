#!/usr/bin/env python3
"""Multi-core charts: for each file, single stream vs parallel blocks vs Harrington sets (and other tools
run with blocks). Left bar = 1,000,000 bits down to X; right bar = decode time. Reads multicore.json."""
import json, os, textwrap
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
import plots as P
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = P.OUT
def main(pdf=None):
    d = json.load(open(os.path.join(HERE, 'multicore.json'))); rows = d['rows']
    tools = []
    for r in rows:
        fam = r[2].split(' ')[0]
        if fam not in tools: tools.append(fam)
    figs = []
    for fam in tools:
        R = [r for r in rows if r[2].split(' ')[0] == fam]
        files = []
        for r in R:
            if r[0] not in files: files.append(r[0])
        variants = []
        for r in R:
            if r[2] not in variants: variants.append(r[2])
        cols = ['#8a8a85', '#2a78d6', '#6250d6', '#eb6834']
        fig, axs = plt.subplots(1, 2, figsize=(13, max(6.0, 0.9 * len(files) * len(variants) / 2 + 3.2)))
        fig.subplots_adjust(left=0.14, right=0.97, top=0.82, bottom=0.12, wspace=0.35)
        fig.text(0.02, 0.95, f'{fam}: one core vs multi-core', fontsize=14, fontweight='bold', color=P.INK)
        fig.text(0.02, 0.905, textwrap.fill('Left: 1,000,000 bits down to X (shorter is smaller). Right: decode time (shorter is faster). '
                 'Measured on a 2-core machine; more cores give more speed. Every file decoded back exactly.', 150), fontsize=9, color=P.INK2)
        H = 0.8 / len(variants)
        for ai, (ax, key, lab) in enumerate(((axs[0], 'size', '1,000,000 bits down to ...'), (axs[1], 'dt', 'Decode time (s)'))):
            for vi, v in enumerate(variants):
                ys, xs = [], []
                for fi, f in enumerate(files):
                    r = [x for x in R if x[0] == f and x[2] == v]
                    if not r: continue
                    r = r[0]; val = r[3] / r[1] * 1e6 if key == 'size' else r[5]
                    y = fi + (vi - (len(variants) - 1) / 2) * H
                    ax.barh(y, val, height=H * 0.9, color=cols[vi % 4], edgecolor='white', label=v if fi == 0 else None)
                    ax.text(val, y, f' {val:,.0f}' if key == 'size' else f' {val:.1f} s', va='center', fontsize=7.5, color=P.INK)
            ax.set_yticks(range(len(files))); ax.set_yticklabels(files); ax.invert_yaxis(); ax.set_xlabel(lab)
            ax.grid(True, axis='x', color=P.GRID, lw=0.6); ax.set_axisbelow(True)
            for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
            xm = ax.get_xlim()[1]; ax.set_xlim(0, xm * 1.18)
        axs[0].legend(frameon=False, fontsize=8, loc='lower right')
        name = f'multicore_{fam.lower().replace("-", "")}.png'
        fig.savefig(os.path.join(OUT, name), dpi=150)
        if pdf: pdf.savefig(fig)
        plt.close(fig); figs.append(name)
    return figs
if __name__ == '__main__': print(main())
