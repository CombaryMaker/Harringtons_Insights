#!/usr/bin/env python3
"""make_top_performers.py - Top_Performers PDF: best and second-best tool for every data type and speed tier.
(c) 2026 Michael Hugh Harrington. usage: make_top_performers.py final-rankings.csv tier-matrix.csv OUT.pdf
Data: the same-machine campaign of 3-4 Nov 2026 (one machine, one session, so speeds are comparable).
Tier rule (Compression Tier Standard v2.0, 1-2-5 series): M = LZ4 decode speed on that data type / the tool's
decode speed. A tool counts in every tier whose limit it meets ("this speed or faster")."""
import sys, csv, glob, collections
from reportlab.lib.pagesizes import letter
from reportlab.platypus import SimpleDocTemplate, Paragraph, Table, TableStyle, Spacer, PageBreak, KeepTogether
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib import colors
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
f = lambda n: glob.glob(f'/usr/share/fonts/**/{n}', recursive=True)[0]
pdfmetrics.registerFont(TTFont('DV', f('DejaVuSans.ttf'))); pdfmetrics.registerFont(TTFont('DVB', f('DejaVuSans-Bold.ttf')))
from reportlab.pdfbase.pdfmetrics import registerFontFamily; registerFontFamily('DV', normal='DV', bold='DVB')
rank, mat, out = sys.argv[1:4]
EXCL = ('precomp', 'mzip', 'x264')       # HX combinations that depend on these cannot ship; same rule as the tier matrix
OURS = {'ours', 'modified', 'mutant/research'}
M = list(csv.DictReader(open(mat))); types = list(dict.fromkeys(r['data_type'] for r in M))
ref, upper = {}, {}
for r in M:
    upper[int(r['tier'])] = float(r['M_upper']) if r['M_upper'] else None
    for a, b in (('hx_M', 'hx_decomp_MBps'), ('other_M', 'other_decomp_MBps')):
        if r[a] and r[b] and r['data_type'] not in ref: ref[r['data_type']] = float(r[a]) * float(r[b])
rows = collections.defaultdict(dict)
for r in csv.DictReader(open(rank)):
    if 'same-machine campaign 3-4 Nov 2026' not in r['corpus'] or not r['ratio'] or not r['decomp_MBps']: continue
    t = r['corpus'].split(' | ', 1)[1].replace(' - same-machine campaign 3-4 Nov 2026', '')
    if r['group'] in OURS and any(x in r['tool'].lower() for x in EXCL): continue
    k = r['tool']; v = dict(tool=k, ours=r['group'] in OURS, ratio=float(r['ratio']), dec=float(r['decomp_MBps']),
                            comp=float(r['comp_MBps']) if r['comp_MBps'] else None)
    if t not in rows or k not in rows[t] or v['ratio'] > rows[t][k]['ratio']: rows[t][k] = v
miss = [t for t in types if t not in rows or t not in ref]
S = lambda **k: ParagraphStyle('x', fontName='DV', **k)
st_t, st_h, st_c, st_n = S(fontSize=17, leading=21, spaceAfter=6), S(fontSize=12.5, leading=15, spaceBefore=4, spaceAfter=4, textColor=colors.HexColor('#1a3d6d')), S(fontSize=9, leading=11.2), S(fontSize=9.5, leading=12.5)
def cell(v):
    if not v: return Paragraph('<font color="#888">no tool this fast</font>', st_c)
    c = f" · comp {v['comp']:.1f} MB/s" if v['comp'] else ''
    txt = f"{v['tool']}<br/><font size=11>{v['ratio']:.4f}×</font><br/><font size=8 color='#333'>decode {v['dec']:.1f} MB/s{c}</font>"
    return Paragraph(f"<b>{txt}</b>" if v['ours'] else txt, st_c)
def fm(x): return f'{x:,.0f}' if x >= 10 else f'{x:.1f}' if x >= 1 else f'{x:.2f}'
story = [Paragraph('Top_Performers - best and second best, every data type and speed tier', st_t),
         Paragraph('Michael Hugh Harrington - 6 November 2026 - HX is Patent Pending (U.S. Provisional App. 64/192,843)', st_n), Spacer(1, 6),
         Paragraph('<b>Bold</b> = Michael Harrington\'s own software (HX, Harrington\'s Sets / hs_zstd, the cascade, HCM, the -hi tools and other modified or research builds) '
                   'finishing first or second. Each cell shows the tool, its compression ratio (original size / compressed size) and, just under it, its decode and compress speed.', st_n), Spacer(1, 4),
         Paragraph('<b>Speed range:</b> tiers follow the 1-2-5 series of the Compression Tier Standard v2.0. M = LZ4\'s decode speed on that data type divided by the tool\'s decode speed. '
                   'A tool counts in every tier whose limit it meets, so each tier means "this speed or faster"; the slowest tier is the overall best. '
                   'All figures come from one machine and one session (same-machine campaign, 3-4 Nov 2026). HX combinations that depend on precomp, mzip or x264 are left out because they cannot ship. '
                   'cmix and paq8px are not included.', st_n), Spacer(1, 4)]
score = collections.Counter()
for ti, t in enumerate([t for t in types if t not in miss]):
    tools = sorted(rows[t].values(), key=lambda v: -v['ratio']); rf = ref[t]
    data = [[Paragraph('<b>Speed range</b>', st_c), Paragraph('<b>Best</b>', st_c), Paragraph('<b>Second best</b>', st_c)]]
    sty = [('GRID', (0, 0), (-1, -1), 0.4, colors.HexColor('#999')), ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#dde6f2')), ('VALIGN', (0, 0), (-1, -1), 'TOP')]
    prev = None
    for k in sorted(upper):
        u = upper[k]; ok = [v for v in tools if u is None or rf / v['dec'] <= u]
        a, b = (ok + [None, None])[:2]
        if a and a['ours']: score['first'] += 1
        if b and b['ours']: score['second'] += 1
        score['cells'] += 1
        rng = f"Tier {k}: M ≤ {u:g}<br/><font size=8>decode ≥ {fm(rf / u)} MB/s</font>" if u else f"Tier {k}: any speed<br/><font size=8>(no limit)</font>"
        data.append([Paragraph(rng, st_c), cell(a), cell(b)])
        if a and a['ours']: sty.append(('BACKGROUND', (1, len(data) - 1), (1, len(data) - 1), colors.HexColor('#e6f4e6')))
        if b and b['ours']: sty.append(('BACKGROUND', (2, len(data) - 1), (2, len(data) - 1), colors.HexColor('#e6f4e6')))
    tb = Table(data, colWidths=[110, 213, 213], repeatRows=1); tb.setStyle(TableStyle(sty))
    story.append(KeepTogether([Paragraph(f'{ti + 1}. {t}', st_h), Paragraph(f'<font size=8.5>LZ4 decode speed on this type (M = 1): {fm(rf)} MB/s</font>', st_c), Spacer(1, 3), tb, Spacer(1, 10)]))
summ = Paragraph(f"<b>Summary:</b> across {score['cells']} type-and-tier categories ({len(types) - len(miss)} data types × 10 tiers), "
                 f"Michael Harrington's software is best in {score['first']} and second best in {score['second']}.", st_n)
story.insert(6, summ); story.insert(7, Spacer(1, 6))
def foot(c, d): c.setFont('DV', 7.5); c.drawCentredString(letter[0] / 2, 22, f'Top_Performers - page {d.page} - HX is Patent Pending (U.S. App. 64/192,843) - michaelhh@gmail.com')
SimpleDocTemplate(out, pagesize=letter, leftMargin=38, rightMargin=38, topMargin=40, bottomMargin=40, title='Top_Performers', author='Michael Hugh Harrington').build(story, onFirstPage=foot, onLaterPages=foot)
print(dict(score), 'missing:', miss)
