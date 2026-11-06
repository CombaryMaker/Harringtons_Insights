#!/usr/bin/env python3
"""build_all_results.py - builds All-Software-Test-Results-2026-11-06.txt and final-rankings-2026-11-06.csv
from result files already recorded (no compression is run). Michael Hugh Harrington, 6 Nov 2026.
Usage: python3 build_all_results.py BASE OUT_TXT OUT_CSV
  BASE = the "data compression" folder (or a copy with the same sub-folders)."""
import csv, re, sys, os, json, collections

BASE, OUT_TXT, OUT_CSV = sys.argv[1], sys.argv[2], sys.argv[3]
P = lambda *a: os.path.join(BASE, *a)
L = []                       # output lines
LONG = []
def w(s=''):
    for line in str(s).split('\n'):
        line = line.rstrip()
        while len(line) > 100:
            cut = line.rfind(' ', 0, 100)
            if cut < 20: cut = 100
            L.append(line[:cut].rstrip()); line = '      ' + line[cut:].lstrip()
            LONG.append(L[-1])
        L.append(line)
def rule(ch='='): w(ch * 100)
def head(t, ch='='):
    w(); rule(ch); w(t); rule(ch)

# ------------------------------------------------------------------------------------------------ helpers
G1, G2, G3, G4 = 'original', 'ours', 'modified', 'mutant/research'
GMARK = {G1: ' ', G2: '*', G3: 'm', G4: 'r'}
def fmt_int(n): return f"{n:,}"
def ratio_s(r): return f"{r:.2f}×" if r else 'n/r'
def num(x, d=2):
    if x is None: return 'n/r'
    if x >= 1000: return f"{x:,.0f}"
    if x >= 100: return f"{x:.1f}"
    if x >= 10: return f"{x:.2f}"
    return f"{x:.{d+1}f}" if x < 1 else f"{x:.{d}f}"

class Row:
    def __init__(s, tool, group, orig=None, comp=None, ratio=None, cs=None, ds=None, cmb=None, dmb=None,
                 approx=False, note='', full=True):
        s.tool, s.group, s.orig, s.comp, s.cs, s.ds, s.approx, s.note, s.full = tool, group, orig, comp, cs, ds, approx, note, full
        s.ratio = ratio if ratio else (orig / comp if orig and comp else None)
        s.cmb = cmb if cmb is not None else (orig / 1e6 / cs if orig and cs else None)
        s.dmb = dmb if dmb is not None else (orig / 1e6 / ds if orig and ds else None)

class Table:
    def __init__(s, key, title, machine, source, orig=None, files=None, notes=None, rank=True):
        s.key, s.title, s.machine, s.source, s.orig, s.files, s.notes, s.rank = key, title, machine, source, orig, files, notes or [], rank
        s.rows = []
    def add(s, *a, **k):
        if 'orig' not in k and s.orig: k['orig'] = s.orig
        s.rows.append(Row(*a, **k)); return s

TABLES = []
def table(*a, **k):
    t = Table(*a, **k); TABLES.append(t); return t

def per_million(x, orig):        # recorded "per million" figure -> ratio exact, bytes derived
    return dict(ratio=1e6 / x, comp=round(x * orig / 1e6) if orig else None, approx=True)

def print_table(t):
    w(); para(t.title)
    w(f"  machine: {t.machine}")
    if t.orig: w(f"  original: {fmt_int(t.orig)} bytes" + (f" in {t.files} file(s)" if t.files else ''))
    for chunk in wrap('source: ' + t.source, 96): w('  ' + chunk)
    for n in t.notes:
        for chunk in wrap(n, 96): w('  ' + chunk)
    w(f"  {'tool':<33}{'g':>2} {'compressed B':>13} {'ratio':>7} {'comp s':>8} {'comp MB/s':>9} {'dec s':>8} {'dec MB/s':>8}")
    rows = sorted(t.rows, key=lambda r: (not r.full, -(r.ratio or 0)))
    for r in rows:
        b = ('~' if r.approx else '') + fmt_int(r.comp) if r.comp else 'n/r'
        w(f"  {r.tool:<33.33}{GMARK[r.group]:>2} {b:>13} {ratio_s(r.ratio):>7} {num(r.cs):>8} {num(r.cmb):>9} {num(r.ds):>8} {num(r.dmb):>8}")
        if r.note:
            for chunk in wrap(r.note, 88): w('      ' + chunk)

def wrap(text, width):
    out, cur = [], ''
    for word in text.split(' '):
        if len(cur) + len(word) + 1 > width and cur: out.append(cur); cur = word
        else: cur = (cur + ' ' + word) if cur else word
    if cur: out.append(cur)
    return out

PMR = re.compile(r'([\d,]+) (->|vs) ([\d,]+) per million')
PM1 = re.compile(r'([\d,]+) per million')
def conv(text):
    f = lambda x: f"{1e6 / int(x.replace(',', '')):.2f}\u00d7"
    text = PMR.sub(lambda m: f"{f(m.group(1))} {m.group(2)} {f(m.group(3))}", text)
    return PM1.sub(lambda m: f(m.group(1)), text)
def para(text, indent=''):
    text = conv(text)
    for chunk in wrap(text, 98 - len(indent)): w(indent + chunk)

# ------------------------------------------------------------------------------------------------ naming
HXNAME = {'HX Fast 9': 'HX Fast', 'HX Fast 11': 'HX Fast + audio/colour lines', 'HX Fast 8': 'HX Fast',
          'HX Complete 11': 'HX Complete + audio/colour lines', 'HX Complete 12 + specialists': 'HX Complete + specialists',
          'HX Complete 12 + specialists + x264 (researc': 'HX Complete + specialists + x264',
          'HX Complete 12 + specialists + x264 (research)': 'HX Complete + specialists + x264',
          'OURS H-Deflate + HX Complete 13b': 'H-Deflate + HX Complete + image routes',
          'OURS H-Deflate + HX Complete': 'H-Deflate + HX Complete',
          'OURS precomp -cn + HX Complete': 'precomp -cn + HX Complete',
          'precomp deflate-only -cn + HX Complete 13b (control)': 'precomp deflate-only + HX Complete (ctl)',
          'precomp deflate-only -cn + HX Complete 13b (': 'precomp deflate-only + HX Complete (ctl)',
          'HX lines (hx, entbench)': 'HX lines', 'HX lines + Sets (hxs, entbench)': 'HX lines + Sets',
          'HX Complete + mzip (research)': 'HX Complete + mzip'}
def hxname(t):
    t = t.strip()
    for k in sorted(HXNAME, key=len, reverse=True):
        if t == k or t.startswith(k + ' ') and k.endswith('13b'): return HXNAME[k] + t[len(k):]
    return HXNAME.get(t, t)

def classify(tool, starred):
    t = tool.lower()
    if 'mzip' in t and ('sets' in t or 'hx' in t): return G4
    if 'x264' in t and 'hx' in t: return G4
    if 'precomp' in t and 'hx' in t: return G4
    if starred: return G2
    return G1

# ------------------------------------------------------------------------------------------------ 1. All-Units sets
AU = P('plans', 'All-Units-vs-All-Units-2026-11-04.txt')
SRC_AU = r'plans\All-Units-vs-All-Units-2026-11-04.txt'
sec_re = re.compile(r'^(\S.*) \((cloud)\) - (\d+) file\(s\), ([\d,]+) bytes$')
row_re = re.compile(r'^  ([* ]) (.+?)\s+([\d,]+)\s+([\d.]+)x\s+([\d.]+)\s+([\d.]+)\s*(.*)$')
AUKEY = {'Silesia, 12 files': 'Silesia', 'Calgary corpus, 18 files': 'Calgary', 'Canterbury corpus, 11 files': 'Canterbury',
         'Canterbury large corpus, 3 files': 'Canterbury large', 'Synthetic test files, 5 files': 'Synthetic (ours)',
         'Random Turing machine output (uiq2, Generic Compression Benchmark data), 3 seeds': 'GCB (uiq2)'}
au_tables = {}
cur = None
for line in open(AU, encoding='utf-8').read().split('\n')[:660]:
    m = sec_re.match(line)
    if m:
        name = m.group(1); key = AUKEY.get(name, name)
        cur = table(key, f"{name} - same-machine campaign 3-4 Nov 2026", 'cloud, 2 cores (busy), wall clock', SRC_AU,
                    orig=int(m.group(4).replace(',', '')), files=int(m.group(3)))
        au_tables[name] = cur; continue
    if cur is None: continue
    if line.startswith('  =>') or line.startswith('====='):
        if line.startswith('====='): cur = cur  # keep
        continue
    m = row_re.match(line)
    if not m or m.group(2).strip() == 'tool': continue
    star, tool, b, r, d, c, note = m.groups()
    tool = tool.strip(); note = note.strip()
    orig, full = cur.orig, True
    pm = re.search(r'only (\d+) of (\d+) files \(([\d,]+) bytes\)', note)
    if pm: orig = int(pm.group(3).replace(',', '')); full = False; note = f"partial: {pm.group(1)} of {pm.group(2)} files ({pm.group(3)} B) - not ranked"
    elif note.startswith('threads') or 'different picks' in note or note in ('', '2 jobs'): note = ''
    grp = classify(tool, star == '*')
    cur.add(hxname(tool), grp, orig=orig, comp=int(b.replace(',', '')), cs=float(c) or None, ds=float(d) or None, note=note, full=full)

# rival3 (kanzi, Lepton, Monkey's Audio, OptimFROG, MagicYUV) - same campaign, same machine
RMAP = {'biased': 'Biased test files', 'bin': 'Programs', 'calgary': 'Calgary corpus', 'canterbury': 'Canterbury corpus',
        'esc': 'Environmental', 'gcb': 'Random Turing', 'jpg': 'JPEG photos', 'large': 'Canterbury large',
        'music': 'Music and sounds', 'pcap_small': 'Packet captures, public', 'screen_raw': 'Rendered document',
        'speech': 'Speech', 'sqlite': 'SQLite', 'synthetic': 'Synthetic', 'text': 'Text:', 'vcoded': 'Compressed video',
        'vraw': 'Raw video', 'silesia': 'Silesia'}
agg = collections.OrderedDict()
singles = []
for x in csv.DictReader(open(P('rivals', 'rival3_results.csv'))):
    if x['set'] == 'silesia' and x['tool'] != 'kanzi -l 9': singles.append(x); continue
    a = agg.setdefault((x['set'], x['tool']), [0, 0, 0.0, 0.0, 0, 0])
    a[0] += int(x['in_bytes']); a[1] += int(x['out_bytes']); a[2] += float(x['comp_s'] or 0); a[3] += float(x['dec_s'] or 0)
    a[4] += 1; a[5] += x['exact'] == 'yes'
for (s, tool), (ib, ob, cs, ds, n, ex) in agg.items():
    tgt = [t for nm, t in au_tables.items() if nm.startswith(RMAP[s])]
    if not tgt: continue
    t = tgt[0]; full = ib == t.orig and ex == n
    note = '' if full else (f"partial: {n} of {t.files} files - not ranked" if ib != t.orig else f"{n - ex} of {n} files not exact - not ranked")
    t.add(tool, G1, orig=ib, comp=ob, cs=cs, ds=ds, full=full, note=note + (' [rivals\\rival3_results.csv]' if note else ''))
for t in au_tables.values():
    if any(r.tool in ('kanzi -l 9', 'Lepton', "Monkey's Audio -c4000", 'MagicYUV -pred median (FFmpeg)') for r in t.rows):
        t.source += r'; kanzi / Lepton / Monkey' + "'" + r's Audio / OptimFROG / MagicYUV rows: rivals\rival3_results.csv'

# ------------------------------------------------------------------------------------------------ HX per-file CSVs
def rcsv(path): return list(csv.DictReader(open(path)))
hxs = rcsv(P('HU-pretrain-results', 'hx-complete', 'hxc_silesia_results.csv'))
hxo = rcsv(P('HU-pretrain-results', 'hx-complete', 'hxc_ooffice_x86_filter.csv'))
hxc = rcsv(P('HU-pretrain-results', 'hx-complete', 'hxc_calgary_canterbury_large_synthetic.csv'))
SIL = collections.OrderedDict((r['file'], int(r['in_bytes'])) for r in hxs)
SIL_TOTAL = sum(SIL.values())
CAL = ['bib', 'book1', 'book2', 'geo', 'news', 'obj1', 'obj2', 'paper1', 'paper2', 'paper3', 'paper4', 'paper5', 'paper6',
       'pic', 'progc', 'progl', 'progp', 'trans']
CAN = ['alice29.txt', 'asyoulik.txt', 'cp.html', 'fields.c', 'grammar.lsp', 'kennedy.xls', 'lcet10.txt', 'plrabn12.txt',
       'ptt5', 'sum', 'xargs.1']
LRG = ['E.coli', 'bible.txt', 'world192.txt']
SYN = ['dup_random_chunks.bin', 'floats_counters.bin', 'hi16_mix.bin', 'random20.bin', 'switch_1mb.bin']
def tool_key(t):
    if t.startswith('HX Fast'): return 'HX Fast'
    if t.startswith('HX Complete'): return 'HX Complete'
    return t

RNAME = {4: 'BSC', 5: 'bzip3', 6: 'split + BSC', 9: 'HX lines', 10: 'media specialist', 11: 'embedded images', 12: 'LZMA',
         13: 'BCJ2 + LZMA', 14: 'PPMd', 15: 'Brotli', 16: 'HCM route', 17: 'HCM v3.7 route', 18: 'JPEG XL 16-bit image', 64: 'x86 + BSC'}
HXR = []
for c in csv.reader(open(P('hx15', 'hx15_routes.csv'))):
    if len(c) < 27: continue
    rt = {}
    for e in c[-1].split('|'):
        i, sz, sec = e.split(':'); rt[int(i)] = (int(sz), float(sec))
    bb = float(c[20]); cands = [(bb, 9)] + [(v[0], i) for i, v in rt.items() if v[0] > 0]
    best, rid = min(cands)
    HXR.append(dict(set=c[0], file=c[1], n=int(c[2]), best=int(best), route=RNAME.get(rid, str(rid)), t=sum(v[1] for v in rt.values())))
HXRF = {(r['set'], r['file']): r for r in HXR}
# ------------------------------------------------------------------------------------------------ Silesia extra tables
sil_hx = table('Silesia', 'Silesia, 12 files - HX run of 3 Nov 2026 (HX Fast, HX Complete, BSC, bzip3, mzip)',
               'cloud, 2 threads, wall clock, decode best of 3', r'HU-pretrain-results\hx-complete\HX-Complete-and-HX-Fast-9-Silesia-2026-11-03.txt + hxc_*.csv',
               orig=SIL_TOTAL, files=12)
tot = collections.defaultdict(lambda: [0, 0.0, 0.0])
ooff = {r['tool']: r for r in hxo}
for r in hxs:
    k = tool_key(r['tool'])
    if r['file'] == 'ooffice' and k in ('HX Fast', 'HX Complete'):   # x86-filter build (the final HX Fast / Complete)
        rr = ooff['HX Fast 8' if k == 'HX Fast' else 'HX Complete (x86 filter + bzip3)']
    else: rr = r
    a = tot[k]; a[0] += int(rr['out_bytes']); a[1] += float(rr['comp_s']); a[2] += float(rr['dec_s'])
for k, (b, c, d) in tot.items():
    sil_hx.add(k, G2 if k.startswith('HX') else G1, comp=b, cs=c, ds=d)
# mzip from the txt (sizes per million only)
sil_hx.add('mzip', G1, comp=None, ratio=1e6 / 203341, cs=2209.4, ds=24.76,
           note='size recorded as a ratio only in this file; bytes from the All-Units table: 43,095,821')

# desktop HCM v3.6 Windows
t = table('Silesia', 'Silesia, 12 files - HCM v3.6 fast on the 12-thread desktop (30 Oct 2026)',
          'desktop Ryzen 5 7500X3D 6c/12t, Windows, one run each', r'hcm-v36-windows\HCM-v3.6-Windows-Core-Scaling-Results.txt',
          orig=SIL_TOTAL, files=12)
for nm, b, c, d in [('HCM v3.6 fast, single core', 42661041, 268.9, 83.3), ('HCM v3.6 fast, parallel 16 MB blocks', 42883537, 176.9, 57.4),
                    ("HCM v3.6 fast, Harrington's Sets 16 MB", 42962663, 137.7, 46.5), ("HCM v3.6 fast, Harrington's Sets 4 MB", 43646375, 76.1, 27.1)]:
    t.add(nm, G2, comp=b, cs=c, ds=d)

# desktop hs_zstd corpus run
t = table('Silesia', 'Silesia, 12 files - hs_zstd 1.1 corpus run on the desktop (31 Oct 2026)',
          'desktop 12 threads, summed median decode of 3', r'HCM-tests\hszcorp-2026-10-31\results.txt', orig=SIL_TOTAL, files=12)
txt = open(P('HCM-tests', 'hszcorp-2026-10-31', 'results.txt')).read()
cols = [[0, 0.0, 0.0] for _ in range(4)]
for line in txt.split('\n'):
    p = [x.strip() for x in line.split('|')]
    if len(p) == 6 and p[0] in SIL:
        for i in range(4):
            b, c, d, _ = [x.strip() for x in p[2 + i].split('/')]
            cols[i][0] += int(b.replace(',', '')); cols[i][1] += float(c); cols[i][2] += float(d)
for i, nm in enumerate(['LZ4 default', 'zstd default (-3)', 'hs_zstd 1.1, 16 MB blocks', 'hs_zstd 1.1, 4 MB blocks']):
    t.add(nm, G2 if 'hs_' in nm else G1, comp=cols[i][0], cs=cols[i][1], ds=cols[i][2])
t.notes.append('Decode = summed per-file medians; compress = summed per-file times (both recorded per file).')

# TurboBench silesia.tar
t = table('Silesia (silesia.tar)', 'silesia.tar (211,957,760 B) - TurboBench 2026.11.01 and our tools (1 Nov 2026)',
          'cloud, 2 cores; TurboBench reports MB/s, ours wall-clock seconds', r'turbobench-silesia-2026-11-01\results-turbobench.txt, results-ours.txt',
          orig=211957760, files=1)
for line in open(P('turbobench-silesia-2026-11-01', 'results-turbobench.txt'), encoding='utf-8'):
    m = re.match(r'\s+(\d+)\s+[\d.]+%\s+([\d.]+)\s+([\d.]+)\s+\d+\S*\s+(.+?)\s+silesia', line)
    if m: t.add(m.group(4).strip(), G1, comp=int(m.group(1)), cmb=float(m.group(2)), dmb=float(m.group(3)))
for line in open(P('turbobench-silesia-2026-11-01', 'results-ours.txt'), encoding='utf-8'):
    m = re.match(r'(.+?)\s+([\d,]+)\s+[\d.]+%\s+([\d.]+)\s+([\d.]+)\s+yes', line)
    if m: t.add(m.group(1).strip(), G2, comp=int(m.group(2).replace(',', '')), cs=float(m.group(3)), ds=float(m.group(4)))

# Silesia 4 GB race
t = table('Silesia 4 GB', 'Silesia repeated to 4 GB (4,026,833,020 B) - hs_zstd race (1 Nov 2026)',
          'desktop 12 threads, decode median of 5', r'hs-zstd-race-4gb-2026-11-01\results-silesia-4gb.txt', orig=4026833020, files=1)
t2 = table('Mixed 3.2 GB tar', 'Mixed public tar 3.2 GB (3,179,306,496 B: Linux, Blender, pdf.js, NYT CSV) - hs_zstd race',
           'desktop 12 threads, decode median of 5', r'hs-zstd-race-4gb-2026-11-01\results-mixed-3.2gb.txt', orig=3179306496, files=1)
for fn, tt in (('results-silesia-4gb.txt', t), ('results-mixed-3.2gb.txt', t2)):
    for line in open(P('hs-zstd-race-4gb-2026-11-01', fn), encoding='utf-8', errors='replace'):
        m = re.match(r'(.+?)\s+bits/M\s+[\d,]+\s+bytes\s+([\d,]+)\s+comp\s+([\d.]+) s.*?decode med\s+([\d.]+) s', line)
        if m:
            nm = m.group(1).strip(); g = G2 if ('hs_zstd' in nm or 'plain blocks' in nm or '1.2 rule' in nm) else G1
            if nm.startswith('plain blocks'): nm = 'hs_zstd plain-blocks control ' + nm.split(',')[1].strip()
            if nm.startswith('1.2 rule'): nm = 'hs_zstd 1.2 rule, 16 MB'
            tt.add(nm, g, comp=int(m.group(2).replace(',', '')), cs=float(m.group(3)), ds=float(m.group(4)))
    tt.notes.append('Compress times as recorded by the race script; zstd --long decode swung widely between rounds.')

# HCM 3.7 max and kanzi -l 9 per file (setsort run) - Silesia totals
hcm37 = {r['file']: r for r in rcsv(P('setsort-reorder-2026-11-04', 'hcm.csv')) if r['set'] == 'silesia'}
kz = {r['file']: r for r in rcsv(P('setsort-reorder-2026-11-04', 'kanzi.csv')) if r['set'] == 'silesia'}
for k in list(hcm37): hcm37[os.path.basename(k)] = hcm37.pop(k)
for k in list(kz): kz[os.path.basename(k)] = kz.pop(k)
t = table('Silesia', 'Silesia, 12 files - HCM v3.7 max (research build) vs kanzi -l 9, file order and Sets reorder (4 Nov)',
          'cloud, 2 cores, wall clock', r'setsort-reorder-2026-11-04\hcm.csv, kanzi.csv', orig=SIL_TOTAL, files=12)
def s4(d, a, c, dd): return sum(int(d[f][a]) for f in SIL), sum(float(d[f][c]) for f in SIL), sum(float(d[f][dd]) for f in SIL)
b, c, d = s4(hcm37, 'hcm_fileorder', 'comp_A_s', 'dec_A_s'); t.add('HCM v3.7 max, file order', G4, comp=b, cs=c, ds=d)
b, c, d = s4(hcm37, 'hcm_reordered_plus_map', 'comp_C_s', 'dec_C_s'); t.add('HCM v3.7 max + Sets reorder (all files)', G4, comp=b, cs=c, ds=d)
best = sum(min(int(hcm37[f]['hcm_fileorder']), int(hcm37[f]['hcm_reordered_plus_map'])) for f in SIL)
t.add('HCM v3.7 max, reorder kept only if smaller', G4, comp=best, note='per-file pick of the two rows above; times not summed for the pick')
b, c, d = s4(kz, 'kanzi_fileorder', 'comp_A_s', 'dec_A_s'); t.add('kanzi -l 9', G1, comp=b, cs=c, ds=d)
b, c, d = s4(kz, 'kanzi_reordered_plus_map', 'comp_C_s', 'dec_C_s'); t.add('kanzi -l 9 + Sets reorder', G4, comp=b, cs=c, ds=d)

# Silesia earlier races (memory, per-million)
SM = r'memory /areas/hcm-benchmarks.md (race of %s); PC Documents\data compression\tools'
t = table('Silesia', 'Silesia, 12 files - Tier-4 race (27 Oct 2026)', 'cloud, 2 cores, decode seconds', SM % '27 Oct', orig=SIL_TOTAL, files=12)
for nm, g, x, d in [('HCM v3.3 fast', G2, 202540, 186), ('HCM v3.3 max', G2, 199186, 217), ('ZPAQ -m4', G1, 201133, 161),
                    ('ZPAQ -m5', G1, 184547, 554), ('bzip3', G1, 221432, 23), ('BSC', G1, 223783, 8.3), ('BSC -b64e2', G1, 219577, 12.5),
                    ('hcmx fast (trit2)', G2, 200682, 241), ('hcmx fast best-of incl. HCM-Bin', G2, 199727, 276),
                    ('HCM v3.3 fast + x86 E8/E9 filter', G2, 201231, None), ('HCM v3.3 max + x86 E8/E9 filter', G2, 197862, None),
                    ('hcmx-s fast (x86 inside format)', G2, 198424, None), ('hcmx-s trit2 lite', G2, 201064, None)]:
    t.add(nm, g, ds=d, **per_million(x, SIL_TOTAL), note='' if d else 'decode total not recorded')
t.notes.append('Compression times not recorded for this race.')

t = table('Silesia', 'Silesia, 12 files - LZMA / 7-Zip / RAR king races and record delta (26-27 Oct 2026)',
          'cloud, in-process decode (LZMA, xz) or CLI (7-Zip, RAR); totals in ms', SM % '26-27 Oct', orig=SIL_TOTAL, files=12)
for nm, g, x, d in [('LZMA -9 (7-Zip SDK, king)', G1, 230033, 2.939), ('lzma-hi', G3, 227126, 2.889),
                    ('best of lzma-hi / xz-hi per file', G3, 224066, 2.924), ('7-Zip Ultra -mx9', G1, 230055, 3.483),
                    ('7z-hi', G3, 223334, 3.314), ('RAR 7.30b2 -m5 -ma5', G1, 250685, 1.059), ('RAR no-pain search (rar-hi)', G3, 246163, 1.019),
                    ('gzip -9', G1, 319742, None), ('gzip -9 + record delta', G4, 313600, None), ('zstd -19', G1, 249563, None),
                    ('zstd -19 + record delta', G4, 248035, None), ('xz -9e', G1, 228633, None), ('xz -9e + record delta', G4, 226836, None),
                    ('zstd -1', G1, 345522, None), ('zstd -1 + record delta', G4, 337279, None), ('gzip -1', G1, 366250, None),
                    ('gzip -1 + record delta', G4, 362891, None), ('xz -0', G1, 295324, None), ('xz -0 + record delta', G4, 292009, None)]:
    t.add(nm, g, ds=d, **per_million(x, SIL_TOTAL), note='' if d else 'decode total not recorded')
t.rank = True
t.notes.append('Rows come from separate races; compare speeds only inside each pair (stock vs our version).')

# Silesia 1 MB sample, Canterbury, Goodwin synthetic (results.json)
rj = json.load(open(P('benchmark-charts-2026-10-29', 'results.json')))
KIND = {'Standard': G1, 'Modified': G3, 'Original': G2, 'Mutant': G4}
bysets = collections.OrderedDict()
for k, v in rj.items():
    f = v['files']; o = sum(x['orig'] for x in f.values()); c = sum(x['comp'] for x in f.values())
    cs = sum(x['ct'] for x in f.values()); ds = sum(x['dt'] for x in f.values())
    bysets.setdefault(v['set'], []).append((v['tool'], KIND[v['kind']], o, c, cs, ds, len(f)))
JKEY = {'Canterbury': 'Canterbury', 'Silesia 1 MB sample': 'Silesia 1 MB sample', 'Synthetic (Goodwin)': 'Goodwin synthetic'}
for s, rows in bysets.items():
    t = table(JKEY[s], f"{s} - 14 tools, one fresh same-machine run (29 Oct 2026)", 'cloud, wall clock per file, summed',
              r'benchmark-charts-2026-10-29\results.json (GitHub benchmarks\charts-2026-10-29)', orig=rows[0][2], files=rows[0][6])
    for nm, g, o, c, cs, ds, n in rows: t.add(nm, g, comp=c, cs=cs, ds=ds)

# Mahoney race (Calgary, Canterbury large, GCB, enwik9)
ms = open(P('HCM-tests', 'mahoney-sets-2026-11-02', 'results_summary.txt'), encoding='utf-8').read()
MK = {'Calgary corpus': ('Calgary', 3251493, 18), 'Canterbury large corpus': ('Canterbury large', 11159482, 3),
      'Generic Compression Benchmark': ('GCB (uiq2)', 19552881, 3), 'enwik9': ('enwik9', 1000000000, 1)}
cur = None
for line in ms.split('\n'):
    for k, (key, o, n) in MK.items():
        if line.startswith(k):
            cur = table(key, f"{line.strip()} - Matt Mahoney test sets race (2 Nov 2026)", 'desktop 12 threads (WSL), wall clock',
                        r'HCM-tests\mahoney-sets-2026-11-02\results_summary.txt (GitHub tools\mahoney_race_2026-11-02)', orig=o, files=n)
    m = re.match(r'\s+(\*?)(\S.*?)\s+([\d,]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+yes', line)
    if m and cur:
        nm = m.group(2).strip()
        g = G1 if m.group(1) == '*' else (G3 if nm.startswith('zpaq-hi') else G4 if nm.startswith('lpaq1-h') else G2)
        cur.add(nm, g, comp=int(m.group(3).replace(',', '')), cs=float(m.group(5)), ds=float(m.group(6)))
for t in TABLES:
    if 'Mahoney' in t.title and t.key == 'enwik9': pass
# The Mahoney machine: check the summary header (desktop WSL per memory); note it
for t in TABLES:
    if 'Mahoney' in t.title: t.machine = 'desktop (run in the PC test folder HCM-tests), wall clock'

# enwik8 (memory, per-million, enwik8 = 100,000,000 B so bytes = X * 100)
t = table('enwik8', 'enwik8 (100,000,000 B) - race of 27 Oct 2026', 'cloud, 2 cores, decode seconds',
          r'memory /areas/hcm-benchmarks.md (enwik8 race, 27 Oct)', orig=100000000, files=1)
for nm, g, x, d in [('HCM v3.3 fast', G2, 221872, 99), ('HCM v3.3 max', G2, 217010, 115), ('ZPAQ -m4', G1, 207405, 102),
                    ('ZPAQ -m5', G1, 196250, 282), ('BSC -b64e2', G1, 214805, 6.0), ('bzip3', G1, 226775, 13.9)]:
    t.add(nm, g, ds=d, **per_million(x, 1e8))
t.notes.append('Compression times not recorded. Also recorded elsewhere: xman (encode.su) ran HCM v3.4 max on his Xeon: '
               '21,477,536 B (4.66×); HCM v3.6 max 21,692,518 B (4.61×), 1.8x faster compress than v3.4 max (no absolute '
               'times recorded). lpaq1 -9 whole file in the pre-training run ~19,755,900 B (5.06×, no times).')

# Silesia 3 MB heads - paq8px wrapper matrix
pw = open(P('HCM-tests', 'paq8px-wrapper-2026-10-30', 'results.txt')).read()
t = table('Silesia 3 MB heads', 'First 3 MB of 10 Silesia files (31,457,280 B) - paq8px through hs_wrap (30 Oct 2026)',
          'desktop 12 threads, wall clock', r'HCM-tests\paq8px-wrapper-2026-10-30\results.txt', orig=31457280, files=10)
for m in re.finditer(r'--- (T\d): (.+?)\n(?:.*\n)*?T\d TOTAL \| \d+ \| (\d+) \| \d+ \| compress ([\d.]+) s \| decompress ([\d.]+) s', pw):
    nm = m.group(2).replace('wrapper, ', 'hs_wrap+').replace(' (baseline)', '').replace(' (cost of splitting alone)', '').replace(' blocks', '').replace(' (sets only)', ' Sets').replace(', ', ' ')
    t.add(nm, G1 if 'single stream' in nm else G2, comp=int(m.group(3)), cs=float(m.group(4)), ds=float(m.group(5)))
t.notes.append('The only recorded paq8px runs (level -1 and -6, 3 MB heads). This is a wrapper speed test, not a full paq8px '
               'benchmark; full-strength paq8px / cmix comparisons are in section 8.')

# other desktop / cloud data sets with bytes recorded
def simple_table(key, title, machine, src, orig, files, rows, notes=()):
    t = table(key, title, machine, src, orig=orig, files=files, notes=list(notes))
    for nm, g, b, c, d in rows: t.add(nm, g, comp=b, cs=c, ds=d)
    return t
simple_table('Plant data', 'Industrial plant-data exports, tar (163,870,720 B, 7 public UCI/NAB data sets) - 1 Nov 2026', 'cloud, 2 cores',
             r'plant-data-2026-11-01\results-plant.txt', 163870720, 7,
             [('gzip -9', G1, 30563416, 94.5, 2.05), ('zstd -19', G1, 22989339, 135.4, 0.34), ('xz -9', G1, 21919100, 161.4, 2.21),
              ('zstd -19 -T2', G1, 22989339, 79.6, 0.22), ('hs_zstd -19 plain blocks 4 MB', G2, 23372624, 51.2, 0.27),
              ("hs_zstd -19 Harrington's Sets 4 MB", G2, 23375395, 52.4, 0.24), ('hs_zstd -19 plain blocks 16 MB', G2, 23010511, 67.6, 0.29),
              ("hs_zstd -19 Harrington's Sets 16 MB", G2, 23052992, 65.4, 0.28), ('HCM v3.6.1 fast (one core)', G2, 17987330, 638.9, 120.65),
              ("HCM v3.6.1 Harrington's Sets 16 MB, 2 cores", G2, 17675237, 240.7, 62.11)])
simple_table('Seismic', 'Seismic miniSEED, 8 EarthScope station-days (132,250,680 B as int32 samples) - 1 Nov 2026', 'cloud, 2 cores',
             r'bank-seismic-2026-11-01\results-seismic.txt', 132250680, 8,
             [('Steim2 in miniSEED (archive standard)', G1, 42413056, None, 0.39), ('zstd -19 on raw samples', G1, 43266256, 82.9, 0.58),
              ('xz -9 on raw samples', G1, 38160844, 120.6, 3.93), ('difference+shuffle, zstd -19', G1, 34782226, 66.5, 0.75),
              ('difference+shuffle, xz -9', G1, 31139192, 86.7, 3.03), ('record delta + zstd -19', G4, 58013095, 79.7, 1.06),
              ('record delta + xz -9', G4, 49853596, 106.3, 4.24), ('HCM v3.6.1 on raw samples', G2, 28594207, 569.9, 144.97),
              ('difference+shuffle, HCM v3.6.1', G2, 33443709, 379.3, 101.49)],
             ['Steim2 compress time not recorded (archive files as published).'])
for nm, o, rows in [('ACH files tar (64,143,360 B, synthetic NACHA)', 64143360,
                     [('gzip -9', G1, 18378293, 16.6, 0.77), ('zstd -19', G1, 13577145, 70.0, 0.16), ('xz -9', G1, 12875868, 91.6, 1.43),
                      ('record delta + zstd -19', G4, 17707264, 68.1, 0.40), ('record delta + xz -9', G4, 16466516, 85.5, 1.83),
                      ('HCM v3.6.1 fast', G2, 10051902, 229.3, 59.34)]),
                    ('ATM electronic journals tar (55,552,000 B, synthetic)', 55552000,
                     [('gzip -9', G1, 4910341, 2.0, 0.32), ('zstd -19', G1, 3634839, 65.9, 0.09), ('xz -9', G1, 3723224, 35.4, 0.47),
                      ('record delta + zstd -19', G4, 5697147, 52.2, 0.27), ('record delta + xz -9', G4, 4921624, 32.5, 0.70),
                      ('HCM v3.6.1 fast', G2, 1945687, 152.9, 41.01)]),
                    ('Mixed bank backup tar (119,685,120 B, synthetic)', 119685120,
                     [('gzip -9', G1, 23367914, 18.9, 0.84), ('zstd -19', G1, 17413034, 140.1, 0.21), ('xz -9', G1, 16950308, 126.3, 1.87),
                      ('record delta + zstd -19', G4, 25334678, 111.6, 0.71), ('record delta + xz -9', G4, 22761988, 115.2, 2.66),
                      ('HCM v3.6.1 fast', G2, 12126393, 385.9, 102.24)])]:
    simple_table('Bank data', f"Bank data: {nm} - 1 Nov 2026", 'cloud, 2 cores', r'bank-seismic-2026-11-01\results-bank.txt', o, 1, rows)
simple_table('Silesia 10 GB', 'Silesia repeated to 10 GB (10,000,000,000 B) - gzip -6 alone vs hs_wrap 1.4 (31 Oct 2026)',
             'desktop 12 jobs, 16 MB blocks, average of 2', r'HCM-tests\gzip-direct-2026-10-31\results.txt', 10000000000, 1,
             [('gzip -6 (zlib 1.3.1 minigzip)', G1, 3222055858, 283.3, 35.0), ('hs_wrap 1.4 + gzip -6', G2, 3223099680, 61.6, 10.8)])
simple_table('Silesia 0.5 GB', 'Silesia repeated to 0.5 GB (500,000,000 B) - Tier-1 check (31 Oct 2026)',
             'desktop 12 jobs, decode median of 3', r'HCM-tests\lz4-tier-2026-10-31\results.txt', 500000000, 1,
             [('LZ4 1.10.0 default', G1, 240984906, 0.4, 0.42), ('hs_wrap 1.4 + gzip -6', G2, 163598643, 3.5, 0.61),
              ('zstd 1.5.6 default', G1, 159257968, 1.7, 0.48)], ['The zstd row is from the second run in the same file (its own LZ4 median also 0.42 s).'])

# 2 GB / 10 GB pzstd race and 20 GB zstd race (memory, per-million)
t = table('Silesia 2 GB', 'Silesia repeated to 2 GB - hs_zstd vs pzstd (31 Oct 2026)', 'desktop 12 threads, decode median',
          r'HCM-tests\pzstd-2026-10-31\results.txt + memory /areas/combary.md', orig=2000000000, files=1)
for nm, g, x, d in [('zstd -3 (1 thread)', G1, 313334, 1.67), ('pzstd -3 -p 12', G1, 314262, 0.94),
                    ('hs_zstd 1.1, 16 MB blocks', G2, 313652, 0.69), ('hs_zstd 1.1, 4 MB blocks', G2, 314561, 0.63)]:
    t.add(nm, g, ds=d, **per_million(x, 2e9))
t.notes.append('Compression times not carried into the summary; 10 GB run of the same race: pzstd 6.87 s vs hs_zstd 4 MB '
               '4.00 s decode (1.72x).')

# ------------------------------------------------------------------------------------------------ Canterbury memory tables
CANT = 2810784
t = table('Canterbury', 'Canterbury, 11 files - HCM line and standard rivals as first recorded (25-29 Oct 2026)',
          'cloud; sizes recorded relative to the original (bytes derived); decode where recorded', r'memory /areas/combary.md, /areas/hcm-benchmarks.md; '
          r'PC HCM_versions_and_results.md, HCM_Wins_Under_Standard.txt', orig=CANT, files=11)
for nm, g, x, d in [('HCM v2', G2, 148355, None), ('HCM v3 fast', G2, 147504, None), ('HCM v3 max', G2, 146695, None),
                    ('HCM v3.1 fast', G2, 128467, None), ('HCM v3.1 max', G2, 127177, None), ('HCM v3.2 fast', G2, 127681, None),
                    ('HCM v3.2 max', G2, 126756, None), ('HCM v3.3 fast', G2, 125763, None), ('HCM v3.3 max', G2, 124675, None),
                    ('hcm-trit v1', G4, 147611, None), ('hcm-trit v1.1', G4, 128687, None), ('hcm-trit v2 (pointer) 6-order', G4, 124940, None),
                    ('hcm-trit v2 max', G4, 123857, None), ('HCM-Bin v1 (overall)', G4, 127225, None),
                    ('hcmx fast best-of', G2, 124831, None), ('hcmx max best-of', G2, 123701, None),
                    ('HCM v3.3 fast + cmix English dictionary', G4, 120010, None), ('hcm-trit v2 max + cmix dictionary', G4, 118283, None),
                    ('ZPAQ -m5', G1, 128716, None), ('xz -9e', G1, 175424, 0.0321), ('xz-hi', G3, 164663, 0.0248),
                    ('bzip2 -9', G1, 193081, 0.0597), ('bzip2-hi', G3, 189235, 0.0554),
                    ('bzip2-style rebuild + arithmetic coding', G4, 185771, None), ('bzip2 rebuild + candidate escape flag', G4, 179074, None),
                    ('LZMA -9 (7-Zip SDK, king)', G1, 175546, 0.02865), ('lzma-hi', G3, 173010, 0.02855), ('xz-hi (king race)', G3, 164663, 0.02881),
                    ('7-Zip Ultra -mx9', G1, 176072, None), ('7z-hi', G3, 165229, None),
                    ('paq8px v217 -8 (full)', G1, 107450, None), ('paq8px lite model set (lab)', G4, 107768, None),
                    ('zpaq-hi (one file per archive)', G3, 127874, None)]:
    note = ''
    if 'paq8px' in nm: note = 'paq8px lab run of 25 Oct (kennedy 414 s -> 181 s); see section 8'
    if 'dictionary' in nm: note = 'dictionary is GPL-3 and English-only (research)'
    t.add(nm, g, ds=d, note=note, **per_million(x, CANT))
t.notes.append('Decode for xz/bzip2/LZMA rows = summed in-process decode of all 11 files. HCM v3 decode ~6.3-6.5x faster '
               'than ZPAQ -m5; HCM v3.3 decode median 1.85-1.87 s (v3 1.88 s).')
t = table('Canterbury', 'Canterbury - zstd-hi / Brotli-hi encoder search, stock decoders (26 Oct 2026)', 'cloud; sizes only',
          r'memory /areas/hcm-benchmarks.md (zstd-hi / Brotli-hi)', orig=CANT, files=11, rank=False)
for a, b, c in [('zstd -1', 244338, 241529), ('zstd -3', 225758, 224204), ('zstd -9', 212292, 210474), ('zstd -19', 183706, 183619),
                ('zstd -22', 183663, 183446), ('Brotli 5', 201189, 200832), ('Brotli 8', 194131, 193930), ('Brotli 11', 174564, 172876)]:
    t.add(a, G1, **per_million(b, CANT)); t.add(a.replace('zstd', 'zstd-hi').replace('Brotli', 'Brotli-hi'), G3, **per_million(c, CANT))
t.notes.append('Decode with stock decoders; speeds not recorded per level (no-pain checks passed).')
t = table('Canterbury', 'Canterbury, 11 files - zpaq-hi vs stock ZPAQ 7.15 -m5, one file per archive (29 Oct 2026)',
          'cloud, wall clock', r'zpaq-hi\canterbury_results.txt', orig=CANT, files=11)
zs = [0, 0.0, 0.0]; zh = [0, 0.0, 0.0]
for line in open(P('zpaq-hi', 'canterbury_results.txt')):
    m = re.match(r'\S+\s+stock\s+([\d,]+) ct\s+([\d.]+) dt\s+([\d.]+) \| hi\s+([\d,]+) ct\s+([\d.]+) dt\s+([\d.]+)', line)
    if m:
        zs[0] += int(m.group(1).replace(',', '')); zs[1] += float(m.group(2)); zs[2] += float(m.group(3))
        zh[0] += int(m.group(4).replace(',', '')); zh[1] += float(m.group(5)); zh[2] += float(m.group(6))
t.add('ZPAQ 7.15 -m5', G1, comp=zs[0], cs=zs[1], ds=zs[2]); t.add('zpaq-hi -m5', G3, comp=zh[0], cs=zh[1], ds=zh[2])
t.notes.append('Per-file times rounded to 0.1 s in the source; totals summed from them.')

# ------------------------------------------------------------------------------------------------ single files
XR, MR, KEN = 8474240, 9970564, 1029744
t = table('Silesia x-ray', 'Silesia x-ray (8,474,240 B, 12-bit radiograph) - every recorded tool', 'mixed: see notes',
          r'hxc_silesia_results.csv; rivals\rival3_results.csv; HX-Fast-vs-mzip-Speed-2026-11-03.txt; setsort hcm.csv/kanzi.csv; '
          r'hcm-v36-windows; memory /areas/hcm-benchmarks.md', orig=XR, files=1, rank=True)
XF = {r['tool']: r for r in hxs if r['file'] == 'x-ray'}
t.add('HX Fast (cloud 2 thr)', G2, comp=int(XF['HX Fast 8']['out_bytes']), cs=float(XF['HX Fast 8']['comp_s']), ds=float(XF['HX Fast 8']['dec_s']))
t.add('HX Fast (desktop 12 thr, in memory)', G2, comp=int(XF['HX Fast 8']['out_bytes']), cs=2.84, ds=0.0132)
t.add('HX Complete (cloud)', G2, comp=int(XF['HX Complete (Sets + Exchange lines)']['out_bytes']), cs=float(XF['HX Complete (Sets + Exchange lines)']['comp_s']), ds=float(XF['HX Complete (Sets + Exchange lines)']['dec_s']))
t.add('HX Complete, every route (router build)', G2, comp=HXRF[('silesia', 'x-ray')]['best'], cs=HXRF[('silesia', 'x-ray')]['t'],
      note='16-bit image route (JPEG XL lossless inside HX Complete); compress = sum of all route times; decode n/r')
t.add('bzip3 (cloud)', G1, comp=int(XF['bzip3']['out_bytes']), cs=float(XF['bzip3']['comp_s']), ds=float(XF['bzip3']['dec_s']))
t.add('BSC (cloud)', G1, comp=int(XF['BSC']['out_bytes']), cs=float(XF['BSC']['comp_s']), ds=float(XF['BSC']['dec_s']))
for x in singles:
    if x['in_bytes'] == str(XR): t.add(x['tool'] + ' (cloud)', G3 if 'tuned' in x['tool'] else G1, comp=int(x['out_bytes']), cs=float(x['comp_s'] or 0) or None, ds=float(x['dec_s']))
t.add('kanzi -l 9 (cloud)', G1, comp=int(kz['x-ray']['kanzi_fileorder']), cs=float(kz['x-ray']['comp_A_s']), ds=float(kz['x-ray']['dec_A_s']))
t.add('HCM v3.7 max (cloud)', G4, comp=int(hcm37['x-ray']['hcm_fileorder']), cs=float(hcm37['x-ray']['comp_A_s']), ds=float(hcm37['x-ray']['dec_A_s']))
t.add('HCM v3.6 fast single core (desktop)', G2, comp=3666372, cs=10.6, ds=3.7)
t.add('ZPAQ -m5 (cloud)', G1, comp=3669704, ds=38, note='from Highlights-2026-11-04.txt'); t.add('lpaq1 (cloud)', G1, comp=3712145, ds=19, note='from Highlights-2026-11-04.txt')
t.add('zstd -19 (cloud, in-process)', G1, comp=5129823, ds=0.0246); t.add('zstd -19 + record delta 2 (mutant)', G4, comp=4508494, ds=0.0202, note='8 paired runs, decode median; encode ~4.7 -> ~6.1 s')
t.add('xz -9e (desktop)', G1, ds=0.1932, cs=2.01, **per_million(529992, XR)); t.add('mzip (desktop)', G1, cs=33.68, ds=0.6707, **per_million(431554, XR))
t.add('Sets + mzip 12 jobs 4 MB (desktop)', G4, cs=11.78, ds=0.2828, **per_million(436117, XR))
t.add('7z-hi (Delta 2)', G3, **per_million(495307, XR)); t.add('7-Zip -mx9', G1, **per_million(528646, XR))
t.add('cmix v20 (published)', G1, comp=3503868, full=False, note='Compared via existing metrics, not actually tested - not ranked')
t.notes.append('Machines differ by row (marked); rank speeds only among rows of the same machine.')

t = table('Silesia mr', 'Silesia mr (9,970,564 B, MRI DICOM) - every recorded tool', 'mixed: see notes', 'same sources as x-ray table',
          orig=MR, files=1)
MF = {r['tool']: r for r in hxs if r['file'] == 'mr'}
t.add('HX Fast (cloud 2 thr)', G2, comp=int(MF['HX Fast 8']['out_bytes']), cs=float(MF['HX Fast 8']['comp_s']), ds=float(MF['HX Fast 8']['dec_s']))
t.add('HX Fast (desktop 12 thr, in memory)', G2, comp=int(MF['HX Fast 8']['out_bytes']), cs=3.30, ds=0.0138)
t.add('HX Complete, every route (router build)', G2, comp=HXRF[('silesia', 'mr')]['best'], cs=HXRF[('silesia', 'mr')]['t'],
      note='16-bit image route (JPEG XL lossless inside HX Complete); compress = sum of all route times; decode n/r')
t.add('bzip3 (cloud)', G1, comp=int(MF['bzip3']['out_bytes']), cs=float(MF['bzip3']['comp_s']), ds=float(MF['bzip3']['dec_s']))
t.add('BSC (cloud)', G1, comp=int(MF['BSC']['out_bytes']), cs=float(MF['BSC']['comp_s']), ds=float(MF['BSC']['dec_s']))
for x in singles:
    if x['in_bytes'] == str(MR): t.add(x['tool'] + ' (cloud)', G1, comp=int(x['out_bytes']), cs=float(x['comp_s'] or 0) or None, ds=float(x['dec_s']))
t.add('kanzi -l 9 (cloud)', G1, comp=int(kz['mr']['kanzi_fileorder']), cs=float(kz['mr']['comp_A_s']), ds=float(kz['mr']['dec_A_s']))
t.add('HCM v3.7 max (cloud)', G4, comp=int(hcm37['mr']['hcm_fileorder']), cs=float(hcm37['mr']['comp_A_s']), ds=float(hcm37['mr']['dec_A_s']))
t.add('HCM v3.6 fast single core (desktop)', G2, comp=2137406, cs=10.2, ds=3.5)
t.add('ZPAQ -m5 (cloud)', G1, comp=2181313, ds=62, note='Highlights-2026-11-04.txt'); t.add('lpaq1 (cloud)', G1, comp=2203847, ds=30, note='Highlights-2026-11-04.txt')
t.add('mzip (desktop)', G1, cs=38.87, ds=0.7142, **per_million(212393, MR)); t.add('xz -9e (desktop)', G1, cs=3.94, ds=0.1108, **per_million(276003, MR))
t.add('7z-hi (Delta 2)', G3, **per_million(259975, MR)); t.add('7-Zip -mx9', G1, **per_million(275637, MR))
t.add('cmix v20 (published)', G1, comp=1827572, full=False, note='Compared via existing metrics, not actually tested - not ranked')
t.notes.append('Machines differ by row (marked); rank speeds only among rows of the same machine.')

t = table('kennedy.xls', 'Canterbury kennedy.xls (1,029,744 B, spreadsheet, record length 13) - recorded tools',
          'cloud; in-process decode in microseconds where marked', r'hxc_calgary_canterbury_large_synthetic.csv; results.json; '
          r'zpaq-hi\canterbury_results.txt; memory /areas/combary.md; HCM_Wins_Under_Standard.txt', orig=KEN, files=1)
KF = {r['tool']: r for r in hxc if r['file'] == 'kennedy.xls'}
t.add('HX Fast', G2, comp=int(KF['HX Fast 8']['out_bytes']), cs=float(KF['HX Fast 8']['comp_s']), ds=float(KF['HX Fast 8']['dec_s']))
t.add('BSC', G1, comp=int(KF['BSC']['out_bytes']), cs=float(KF['BSC']['comp_s']), ds=float(KF['BSC']['dec_s']))
t.add('bzip3', G1, comp=int(KF['bzip3']['out_bytes']), cs=float(KF['bzip3']['comp_s']), ds=float(KF['bzip3']['dec_s']))
for nm, g, b, d in [('zstd -22', G1, 64815, 0.001359), ('zstd -22 + record delta 13', G4, 26398, 0.000520), ('lz4 -12', G1, 324136, 0.000770),
                    ('lz4 -12 + record delta 13', G4, 43654, 0.000725), ('gzip -9', G1, 209721, 0.002179), ('gzip -9 + record delta 13', G4, 42972, 0.001054),
                    ('gzip -9 + change map', G4, 35558, None), ('xz -9e', G1, 51868, None), ('xz-hi (delta 13)', G3, 23656, None),
                    ('bzip2 -9', G1, 130280, None), ('bzip2-hi (100k blocks)', G3, 120482, None), ('ZPAQ -m5', G1, 16822, 4.3),
                    ('zpaq-hi -m5', G3, 16002, 5.5), ('HCM v3 fast', G2, 53899, None), ('HCM-Bin v1', G4, 16763, None), ('HCM-Bin v1 max (hcmx)', G2, 16797, None),
                    ('HCM v3.3 fast', G2, 18680, None), ('lpaq1 -6', G1, 37943, None), ('lpaq1-h', G4, 21644, None),
                    ('Snappy', G1, None, 0.000966), ('Snappy + record delta 13', G4, None, 0.000717), ('LZFSE', G1, None, 0.001064), ('LZFSE + record delta 13', G4, None, 0.000767)]:
    if b: t.add(nm, g, comp=b, ds=d)
    else: t.add(nm, g, ds=d, **per_million({'Snappy': 410432, 'Snappy + record delta 13': 126015, 'LZFSE': 132346, 'LZFSE + record delta 13': 53260}[nm], KEN))
t.notes.append('kennedy.xls is an extreme outlier (record data); totals elsewhere are given with it included.')

# ------------------------------------------------------------------------------------------------ CSV + rankings helpers
def mshort(t):
    m = t.machine
    return 'desktop' if 'desktop' in m else ('cloud' if 'cloud' in m else 'mixed')
def ranked(t):
    rs = [r for r in t.rows if r.full and r.ratio]
    return sorted(rs, key=lambda r: -r.ratio), sorted([r for r in rs if r.dmb], key=lambda r: -r.dmb)

# ================================================================================================ WRITE DOCUMENT
w('ALL SOFTWARE TEST RESULTS - COMPRESSION RATIO AND SPEED FOR EVERY TOOL TESTED')
w('Author: Michael Hugh Harrington          Date: 6 November 2026')
w('HX is Patent Pending (U.S. App. 64/192,843)')
w('Copyright (c) 2026 Michael Hugh Harrington. HX, HX Fast, HX Complete, the cascade and the per-slice')
w('build: all rights reserved (not MIT). Our other tools: MIT. Third-party tools keep their licences.')
rule()
w('Built only from results already recorded (no new benchmarks were run for this document).')
w()
w('HOW TO READ')
para('Ratio = original bytes / compressed bytes (higher is better), written x.xx×. Speeds are given both as '
     'seconds (as recorded) and as MB/s, where MB = 1,000,000 bytes of ORIGINAL data per second (original size / '
     'recorded time). "n/r" = not recorded. A "~" before a byte count means the source recorded only a size '
     'relative to the original; the ratio is exact, the byte count is derived from it and the known original size '
     '(rounding error under 0.0001%). Every listed result decoded back exactly unless a note says otherwise.', '  ')
para('Group column g: blank = original third-party program, * = our program, m = modified tool (theirs plus our '
     'changes; the stock decoder reads it), r = mutant or research build (needs our decoder, uses a research-only part, '
     'or is a test control).', '  ')
para('Machines: "cloud" = 2-core cloud machine (busy, noisy times); "desktop" = Michael\'s Ryzen 5 7500X3D, 6 cores / '
     '12 threads. Compare speeds only inside one table (same machine, same session).', '  ')
para('Section order: 1 tool list by group; 2 results by corpus; 3 HX; 4 modified tools; 5 mutants and research builds; '
     '6 task-level tests; 7 our programs outside the corpus tables; 8 cmix / paq8px; 9 final rankings; 10 gaps.', '  ')

# ---------------------------------------------------------------- 1 inventory
head('1. EVERY TOOL TESTED, BY GROUP')
inv = collections.defaultdict(set)
for t in TABLES:
    for r in t.rows: inv[r.group].add(re.sub(r' \((cloud|desktop)[^)]*\)$', '', r.tool))
EXTRA = {G1: ['FLAC (xiph) -8', 'WebP lossless', 'PNG (Pillow / optipng-style)', 'qpdf 11.9 (PDF baseline)', 'pdftoppm (render check)',
              'Snappy', 'LZFSE', 'RAR 7.30 beta 2', 'DuckDB Parquet writer', 'Apache ORC C++', 'pigz', 'rapidgzip 0.16.1', 'bgzip (htslib 1.21)',
              'kanzi -l 9', 'mzip (Cranot)', 'Wireshark tshark 4.6.9', 'Zeek 8.0.4', 'Snort 3.12.4', 'ClamAV 1.5.1', 'binwalk 3.1.0', 'YARA',
              'Tesseract 5', 'whisper.cpp', 'Apache Tika 3.3.3', 'jq 1.7', 'GNU grep 3.11 / ripgrep 14.1', 'Ghostscript 10', 'gcc/make (builds)',
              'PrusaSlicer', 'xdelta3 3.0.11', 'pugixml', 'meshoptimizer', 'RecastNavigation', 'river (HalfSpaceTrees)', 'cmix v21 (lab only)',
              'paq8px v217 (lab / wrapper only)'],
         G2: ['HCM v2, v3, v3.1, v3.2, v3.3, v3.4, v3.5, v3.6, v3.6.1 (fast and max)', 'hcmx / hcmx-s', "Harrington's Sets (HCM sets mode)",
              'hs_wrap 1.3 / 1.4 / 1.5', 'hs_zstd 1.0 / 1.1 / 1.2 / 1.3', 'cascade v1.1 (zstd) and HCM cascade v3', 'HX lines, HX Fast, HX Complete',
              'H-Deflate v1', 'hs_shark 1.1 / 1.2', 'hs_zeek (pcap split + merge)', 'hs_scan', 'hs_clam / hs_binwalk', 'hs_ocr', 'hs_whisper',
              'HsTika', 'hs_lines (jq / river)', 'hs_grep 1.0 / 1.1', 'hs_gs', 'hs_build', 'hs_saveload and the game-test wrappers'],
         G3: ['zstd-hi', 'Brotli-hi', 'xz-hi', 'lzma-hi', '7z-hi', 'bzip2-hi', 'zpaq-hi', 'jxl-hi (cjxl settings)', 'JPEG-LS tuned (CharLS)',
              'flac-hi (tuned FLAC)', 'RAR no-pain search', 'PDF stream rewrite (zlib + PNG predictor)', 'zip-hi (results kept, baseline unclear)'],
         G4: ['lpaq1-h (lpaq1 mutant)', 'hcm-trit v1 / v1.1 / v2', 'HCM-Bin v1', 'HCM v3.7 (research)', 'Sets reorder (setsort)',
              'record delta + gzip / zstd / lz4 / xz / Snappy / LZFSE / RAR', 'change map (activity map) + gzip / lz4 / Snappy / LZFSE',
              'Harrington Exchange front ends', 'Parquet / ORC with record columns', 'paq8px lite model set', 'cmix low-memory build',
              'Sets + mzip', 'HX Complete + mzip', 'HX + x264', 'precomp routes + HX Complete', 'bzip2 rebuild + candidate escape flag']}
NAMES = {G1: 'ORIGINAL THIRD-PARTY PROGRAMS', G2: 'OUR PROGRAMS', G3: 'MODIFIED TOOLS (stock decoder reads the output)',
         G4: 'MUTANTS AND RESEARCH BUILDS'}
ntools = 0
for g in (G1, G2, G3, G4):
    names = sorted(set(inv[g]) | set(EXTRA[g]), key=str.lower)
    ntools += len(names)
    w(); w(f"  ({'1234'[[G1, G2, G3, G4].index(g)]}) {NAMES[g]} - {len(names)} entries")
    for chunk in wrap('; '.join(names), 94): w('      ' + chunk)
w(); w('  (5) TASK-LEVEL TESTS (no compressor name): see section 6 - 26 tests.')
w(f"  Entries listed above: {ntools} (one entry per tool and setting as named in the result files).")

# ---------------------------------------------------------------- 2 corpus tables
head('2. RESULTS BY CORPUS (each table = one machine, one session; best ratio first)')
ORDER = ['Silesia', 'Silesia (silesia.tar)', 'Silesia 1 MB sample', 'Silesia 3 MB heads', 'Silesia x-ray', 'Silesia mr', 'Calgary',
         'Canterbury', 'kennedy.xls', 'Canterbury large', 'enwik8', 'enwik9', 'GCB (uiq2)', 'Synthetic (ours)', 'Goodwin synthetic']
def korder(t):
    return (ORDER.index(t.key) if t.key in ORDER else len(ORDER), TABLES.index(t))
done_keys = []
for t in sorted(TABLES, key=korder):
    if t.key not in done_keys:
        done_keys.append(t.key); w(); rule('-'); w(f"CORPUS: {t.key}"); rule('-')
    print_table(t)

# ---------------------------------------------------------------- 3 HX
head('3. HX - ALL HX FAST AND HX COMPLETE RESULTS')
para('What they are. HX splits a file into independent segments of about 1 MB, each decoded on its own core; for every '
     'segment it tries the Harrington Exchange lines (byte lanes, 12-bit samples, context lines, audio predictors), '
     "Harrington's Sets and the cascade's repeat line, and keeps the smallest. HX Fast is the speed-first mode. HX "
     'Complete adds whole-file and split routes with BSC, bzip3, LZMA (BCJ2 for x86), PPMd and Brotli, the x86 filter, '
     'and a learned router. The per-slice build (routing slice by slice) is under construction and has no results yet. '
     'Source-file build labels: hxf 8 / 9 / 11 = HX Fast builds, hxc / 11 / 12 / 13b = HX Complete builds, hx15 = HX '
     'Complete with the learned router, hx / hxs = the HX lines bench (entbench).')
w(); w('3.1 SILESIA, PER FILE (cloud, 2 threads, wall clock; decode best of 3; all exact)')
w(f"  {'file':<9}{'orig B':>11} | {'HX Fast':>10} {'ratio':>7} {'dec s':>6} | {'HX Compl.':>10} {'ratio':>7} {'dec s':>6} | {'chose':<14}")
fsum = csum = 0
for f, o in SIL.items():
    rows = {r['tool']: r for r in hxs if r['file'] == f}
    hf = ooff['HX Fast 8'] if f == 'ooffice' else rows['HX Fast 8']
    hc = ooff['HX Complete (x86 filter + bzip3)'] if f == 'ooffice' else [v for k, v in rows.items() if k.startswith('HX Complete')][0]
    chose = 'x86 filter + bzip3' if f == 'ooffice' else re.search(r'\((.*)\)', hc['tool']).group(1)
    fb, cb = int(hf['out_bytes']), int(hc['out_bytes']); fsum += fb; csum += cb
    w(f"  {f:<9}{o:>11,} | {fb:>10,} {ratio_s(o / fb):>7} {float(hf['dec_s']):>6.3f} | {cb:>10,} {ratio_s(o / cb):>7} {float(hc['dec_s']):>6.3f} | {chose.replace('Exchange lines', 'Exch')[:14]:<14}")
w(f"  {'TOTAL':<9}{SIL_TOTAL:>11,} | {fsum:>10,} {ratio_s(SIL_TOTAL / fsum):>7} {tot['HX Fast'][2]:>6.2f} | {csum:>10,} {ratio_s(SIL_TOTAL / csum):>7} {tot['HX Complete'][2]:>6.2f} |")
w(f"  Compress totals: HX Fast {tot['HX Fast'][1]:.1f} s, HX Complete {tot['HX Complete'][1]:.1f} s. ooffice rows use the x86-filter")
w('  build (HX Fast 2,240,864 B, HX Complete 2,123,508 B; without the filter 2,594,986 / 2,526,899 B).')
w('  Set totals in the same run: mzip 4.92× (dec 24.76 s, comp 2,209.4 s), bzip3 46,426,674 B = 4.57×')
w('  (28.03 s), BSC 4.55× (13.66 s). Source: HX-Complete-and-HX-Fast-9-Silesia-2026-11-03.txt, hxc_*.csv.')

w(); w('3.2 x86 FILTER ON ooffice (6,152,192 B; cloud)  source: hxc_ooffice_x86_filter.csv')
for r in hxo:
    nm = hxname(r['tool'].replace('HX Fast 8', 'HX Fast + x86 filter'))
    w(f"  {nm:<40} {int(r['out_bytes']):>11,} B {ratio_s(6152192 / int(r['out_bytes'])):>7}  comp {float(r['comp_s']):>6.2f} s  dec {float(r['dec_s']):>6.3f} s")
w('  Without the filter: HX Fast 2,594,986 B (2.37×), HX Complete 2,526,899 B (2.43×).')

w(); w('3.3 CALGARY, CANTERBURY, CANTERBURY LARGE, SYNTHETIC - PER FILE (cloud, 2 threads)')
w('  source: HU-pretrain-results\\hx-complete\\hxc_calgary_canterbury_large_synthetic.csv')
for grpname, files in (('Calgary', CAL), ('Canterbury', CAN), ('Canterbury large', LRG), ('Synthetic (ours)', SYN)):
    w(); w(f"  {grpname}")
    w(f"  {'file':<17}{'orig B':>11} | {'HX Fast':>10} {'ratio':>7} {'dec s':>6} | {'HX Compl.':>10} {'ratio':>7} {'dec s':>6} | {'chose':<11}")
    sums = [0, 0, 0, 0.0, 0.0, 0.0, 0.0]
    for f in files:
        rows = {r['tool']: r for r in hxc if r['file'] == f}
        hf = rows['HX Fast 8']; hc = [v for k, v in rows.items() if k.startswith('HX Complete')][0]
        o = int(hf['in_bytes']); fb, cb = int(hf['out_bytes']), int(hc['out_bytes'])
        sums[0] += o; sums[1] += fb; sums[2] += cb; sums[3] += float(hf['dec_s']); sums[4] += float(hc['dec_s'])
        sums[5] += float(hf['comp_s']); sums[6] += float(hc['comp_s'])
        ch = re.search(r'\((.*)\)', hc['tool']).group(1).replace('split: ', '').replace('Exchange lines', 'Exch')
        w(f"  {f[:16]:<17}{o:>11,} | {fb:>10,} {ratio_s(o / fb):>7} {float(hf['dec_s']):>6.3f} | {cb:>10,} {ratio_s(o / cb):>7} {float(hc['dec_s']):>6.3f} | {ch[:11]:<11}")
    w(f"  {'TOTAL':<17}{sums[0]:>11,} | {sums[1]:>10,} {ratio_s(sums[0] / sums[1]):>7} {sums[3]:>6.2f} | {sums[2]:>10,} {ratio_s(sums[0] / sums[2]):>7} {sums[4]:>6.2f} |")
    w(f"  compress totals: HX Fast {sums[5]:.1f} s, HX Complete {sums[6]:.1f} s")

w(); w('3.4 HX ON EVERY DATA SET OF THE 3-4 NOV CAMPAIGN (cloud; best HX Fast and best HX Complete row per set)')
w('  source: plans\\All-Units-vs-All-Units-2026-11-04.txt (full tables in section 2)')
w(f"  {'set':<28} {'HX Fast':>8} {'dec MB/s':>9} | {'HX Compl.':>9} {'dec MB/s':>9} | {'best other':<15} {'ratio':>7}")
for nm, t in au_tables.items():
    fr = [r for r in t.rows if r.full and r.tool.startswith('HX Fast')]
    cr = [r for r in t.rows if r.full and (r.tool.startswith('HX Complete') or 'H-Deflate' in r.tool)]
    oth = [r for r in t.rows if r.full and r.group == G1]
    bf = max(fr, key=lambda r: r.ratio) if fr else None; bc = max(cr, key=lambda r: r.ratio) if cr else None
    bo = max(oth, key=lambda r: r.ratio) if oth else None
    w(f"  {nm[:28]:<28} {ratio_s(bf.ratio) if bf else 'n/r':>8} {num(bf.dmb) if bf else 'n/r':>9} | {ratio_s(bc.ratio) if bc else 'n/r':>9} "
      f"{num(bc.dmb) if bc else 'n/r':>9} | {(bo.tool if bo else 'n/r')[:15]:<15} {ratio_s(bo.ratio) if bo else '':>7}")

w(); w('3.5 HX FAST ON THE DESKTOP - 1 vs 12 THREADS, IN-MEMORY DECODE (3 Nov 2026, best of 3, all exact)')
w('  source: HU-pretrain-results\\speed-desktop\\HX-Fast-vs-mzip-Speed-2026-11-03.txt (sizes recorded as ratios)')
w(f"  {'file':<24}{'orig B':>11} {'ratio':>7} {'dec 1 thr':>10} {'dec 12 thr':>11} {'MB/s 12':>8} | {'mzip ratio':>10} {'mzip dec':>9}")
DSK = [('x-ray', XR, 430952, 0.0675, 0.0132, 431554, 0.6707), ('mr', MR, 207541, 0.0639, 0.0138, 212393, 0.7142),
       ('E_16bit_hi_biased.bin', 8388608, 625315, 0.0092, 0.0051, 625740, 0.0486), ('F_12bit_gauss_packed.bin', 8388608, 532281, 0.0129, 0.0062, 533905, 0.8242),
       ('G_markov_bits.bin', 8388608, 363608, 0.0514, 0.0121, 368768, 0.6685), ('D_bias_per_window.bin', 8388608, 636610, 0.0326, 0.0158, 653920, 0.2505),
       ('ringOsc-nist.bin', 1000000, 79613, 0.0040, 0.0044, 78179, 0.0883), ('H_zipf_with_repeats.bin', 8388608, 567164, 0.0064, 0.0064, 567163, 0.0215),
       ('A_bits_p10.bin', 8388608, 469561, 0.0486, 0.0130, 474193, 0.6763), ('sao', 7251944, 689551, 0.0087, 0.0087, 610276, 0.2005),
       ('osdb', 10085684, 307361, 0.0078, 0.0081, 223019, 0.7804), ('xml', 5345280, 84712, 0.0031, 0.0031, 66481, 0.2631),
       ('dickens', 10192446, 279559, 0.0101, 0.0095, 220139, 0.6964)]
for f, o, x, d1, d12, mx, md in DSK:
    w(f"  {f:<24}{o:>11,} {ratio_s(1e6 / x):>7} {d1:>9.4f}s {d12:>10.4f}s {o / 1e6 / min(d1, d12):>8,.0f} | {ratio_s(1e6 / mx):>10} {md:>8.4f}s")
w('  HX Fast compress (desktop): x-ray 2.84 s, mr 3.30 s, biased files 2.0-3.9 s, sao 2.33 s, osdb 2.98 s, xml 1.70 s,')
w('  dickens 4.28 s. mzip compress 12-75 s. HX Fast decodes 3x to 133x faster than mzip on every file here.')

w(); w('3.6 HX LINES vs THE ENTROPY CODERS - BIASED DATA WITH A KNOWN FLOOR (desktop, 128 MB files, in memory)')
w('  source: HU-pretrain-results\\biased-desktop\\Biased-Data-vs-Entropy-Coders-2026-11-03.txt')
w(f"  {'source':<29} {'floor':>7} | {'best coder':<14} {'ratio':>7} {'MB/s':>6} | {'HX lines':>8} {'MB/s 1 / 12 thr':>16}")
for nm, fl, bc, bx, bm, hx, hm, h12 in [('E 16-bit, high byte biased', 625000, 'Huff0', 739834, 1915, 625360, 2048, 9804),
                                        ('F 12-bit noise, packed', 530764, 'FSE', 759564, 865, 532328, 880, 6328),
                                        ('G bits with memory (Markov)', 362458, 'rANS', 380646, 223, 363572, 172, 1571),
                                        ('D bias changes every 64 KB', 613570, 'adaptive arith', 624283, 23, 615499, 336, 1800)]:
    w(f"  {nm:<29} {ratio_s(1e6 / fl):>7} | {bc:<14} {ratio_s(1e6 / bx):>7} {bm:>6,} | {ratio_s(1e6 / hx):>8} {f'{hm:,} / {h12:,}':>16}")
para('Floor = the exact entropy of the source, shown as the best possible ratio. D uses HX lines + Sets (its speeds '
     'are recorded as ~336 MB/s on 1 thread and ~1,800 MB/s on 12). Plain biased bits and Zipf bytes (A, B, C) tie '
     'the best coder on the floor. NIST ringOsc (1 MB, cloud): HX lines 12.56\u00d7, decode 4.4 ms, vs best coder FSE '
     '8.00\u00d7; BSC 12.74\u00d7 (71 ms) and bzip3 12.72\u00d7 (102 ms).', '  ')

w(); w('3.7 HX FAST ON SPEECH vs FLAC (cloud, 6 FSDD WAVs, 20,997,112 B; decode best of 5)')
w('  source: HU-pretrain-results\\audio-speed\\FLAC-vs-HX-Fast-Speed-2026-11-03.txt (sizes recorded as ratios)')
for nm, x, c, d in [('tuned FLAC (flac-hi, modified)', 517314, None, 0.171), ('HX Fast + audio lines', 529974, 79.54, 0.271), ('flac -8', 531634, 0.38, 0.169)]:
    w(f"  {nm:<34} {ratio_s(1e6 / x):>7}  comp {num(c):>6} s  dec {d:.3f} s ({20.997112 / d:,.0f} MB/s)")
w('  HX Fast build comparison (cloud, 22 files, 144,816,920 B): earlier build 2.23× comp 374 s dec 0.83 s; current')
w('  build 2.24× comp 201 s dec 0.84 s (1.9x faster compress). Source: HX-Fast-8-vs-7-2026-11-03.txt.')

w(); w('3.8 THE LEARNED ROUTER (HX Complete, whole-file routing) - 83 training files + 10 unseen files')
w('  source: hx15\\hx15_routes.csv replayed by hx15\\hx15_eval.py (held-out by set; no compression re-run); hx15_live.csv')
w('  Try every route on all 83 files: 128,913,421 B (2.39×), route time 6,792 s. [ ] = files in pass 1 / 2 / 3.')
for nm, b, r, ts, sc, ps in [('router, plain', 130386842, 2.361, 4529, '+1.143%', '57 / 13 / 13'),
                             ('router + specialist rule', 129793900, 2.371, 4369, '+0.683%', '60 / 15 / 8'),
                             ('router + specialist + novelty rule', 129008241, 2.386, 5029, '+0.074%', '41 / 9 / 33')]:
    w(f"  {nm:<35}{b:>12,} B {r:.2f}\u00d7 {ts:>6,} s ({100 * (1 - ts / 6792):4.1f}% saved) size {sc:>7} passes {ps}")
w('  The same 0.074% / 0.683% figures and pass counts are recorded in the provisional draft (section 9.7).')
w(); w('  Live run on files the router had never seen (learning on; each decoded and compared byte for byte):')
w(f"  {'set':<11}{'file':<34}{'orig B':>11}{'HX B':>11}{'ratio':>8}{'comp s':>8}{'dec s':>8}")
lt = [0, 0, 0.0, 0.0]
for r in rcsv(P('hx15', 'hx15_live.csv')):
    o, b, c, d = int(r['in_bytes']), int(r['out_bytes']), float(r['comp_s']), float(r['dec_s'])
    lt[0] += o; lt[1] += b; lt[2] += c; lt[3] += d
    w(f"  {r['set']:<11}{r['file'][:33]:<34}{o:>11,}{b:>11,}{ratio_s(o / b):>8}{c:>8.2f}{d:>8.3f}")
w(f"  {'TOTAL (10 of 30 listed files run)':<45}{lt[0]:>11,}{lt[1]:>11,}{ratio_s(lt[0] / lt[1]):>8}{lt[2]:>8.1f}{lt[3]:>8.2f}")

w(); w('3.9 HX COMPLETE TRYING EVERY ROUTE (learned-router build, training run; cloud, 2 threads)')
w('  source: hx15\\hx15_routes.csv (smallest route per file; time = sum of all route times; decode not recorded here)')
w(f"  {'set':<11}{'file':<29}{'orig B':>12}{'HX Complete B':>14}{'ratio':>9}  {'winning route':<22}")
cur = None; st = [0, 0, 0.0]
def flush():
    if cur: w(f"  {'':<11}{'set total':<29}{st[0]:>12,}{st[1]:>14,}{ratio_s(st[0] / st[1]):>9}  route time {st[2]:,.0f} s")
for r in HXR:
    if r['set'] != cur:
        flush(); cur = r['set']; st = [0, 0, 0.0]
    st[0] += r['n']; st[1] += r['best']; st[2] += r['t']
    w(f"  {r['set']:<11}{r['file'][:28]:<29}{r['n']:>12,}{r['best']:>14,}{ratio_s(r['n'] / r['best']):>9}  {r['route'][:22]:<22}")
flush()
para('Standouts: Silesia mr 5.61\u00d7 and x-ray 2.39\u00d7 (both smaller than JPEG-LS and stock JPEG XL), ooffice 3.31\u00d7, '
     'samba 6.47\u00d7, xml 15.34\u00d7. The HCM v3.7 route is a private research build.', '  ')

# ---------------------------------------------------------------- 4 modified
head('4. MODIFIED TOOLS (-hi tools) - stock decoders read every output')
para('Full corpus rows for these tools are in section 2 (Silesia LZMA/7-Zip/RAR race, Canterbury tables, Silesia 1 MB '
     'sample, Goodwin synthetic, Mahoney race, x-ray, mr, kennedy.xls). Results recorded only outside those tables:', '  ')
MODS = [
 ('xz-hi', 'Canterbury 175,424 -> 164,663 per million; decode 32.1 -> 24.8 ms; Goodwin G_audio16 '
  '439,256 -> 382,084 B (decode 23.1 -> 18.1 ms); modern data 12 files 4,053,776 -> 4,040,884 B, decode 389 -> 360 ms.'),
 ('bzip2-hi', 'Canterbury 193,081 -> 189,235 per million, decode 59.7 -> 55.4 ms; compress much slower '
  '(plrabn12 0.07 -> 22.9 s).'),
 ('lzma-hi', 'Canterbury 175,546 -> 173,010 per million, 28.65 -> 28.55 ms; nci 51,891 -> 43,034 per '
  'million and 1.22x faster decode.'),
 ('7z-hi', 'Canterbury 176,072 -> 165,229 per million; ooffice 394,261 -> 329,360 per million (1.25x faster, '
  'BCJ2); nci 51,900 -> 43,047 per million (1.20x faster); mr 3.63× -> 3.85×; x-ray 1.89× -> 2.02×.'),
 ('zstd-hi / Brotli-hi', 'small no-pain gains only (Canterbury table, section 2); zstd-hi counties export compress 72.9 '
  '-> 36.3 s.'),
 ('jxl-hi (stock cjxl effort 7, other settings searched)', 'x-ray: +0.3% size with 5.3x faster decode, or 2.3% smaller '
  'with 1.35x faster; RG1 +2.9% / 7.5x; MR2 +3.6% / 4.8x; RG3 0.5% smaller / 1.30x; OBXXXX1A +4.8% / 1.37x. '
  'Lost on 693, MR-SIEMENS, ALOKA, eCT.'),
 ('JPEG-LS tuned (CharLS)', 'x-ray 3,591,153 -> 3,574,443 B (2.36× -> 2.37×), decode 103.6 -> 87.6 ms (cloud '
  'rival3 run: 0.120 -> 0.100 s).'),
 ('flac-hi (tuned FLAC)', '6 speech WAVs: flac -8 531,634 -> 520,279 per million, decode 136 -> 123 '
  'ms; later run 517,314 per million, decode 0.171 s.'),
 ('RAR no-pain search', 'Silesia 250,685 -> 246,163 per million, decode 1,059 -> 1,019 ms; sao '
  '764,292 -> 682,560 per million with -mcd-, decode 68.0 -> 61.5 ms.'),
 ('zpaq-hi', 'Canterbury 361,792 -> 359,424 B (one file per archive), compress 11.5 -> 20.9 s, decode 11.8 -> 14.1 s '
  '(size win only); Calgary 716,354 B, Canterbury large 2,151,206 B (smallest of all there), GCB 9,137,324 B, enwik9 '
  '168,215,145 B (section 2, Mahoney race). Same-block multi-core: 0.1-1.1% smaller than stock, decodes 1-37% slower.'),
 ('PDF stream rewrite', '139 pdf.js PDFs: originals 1.00×, qpdf known optimisation 851,978 per million, ours '
  '837,340 per million; render 11.22 / 11.35 / 11.60 s; strict no-pain pick 846,683 per million at 11.20 s render.'),
 ('WebP / PNG searches', 'WebP lossless -z 9 on 24 Kodak images: 395,741 -> 395,670 per million (no real gain); PNG '
  'per-image filter + zlib strategy 558,414 -> 550,934 per million, decode 225 -> 202 ms (known optipng work, not counted).')]
for nm, txt in MODS:
    w(); para(nm + ': ' + txt, '  ')
w(); w('  source for this section: memory /areas/hcm-benchmarks.md and /areas/combary.md; PC folders xz-hi, bzip2-hi,')
w('  zpaq-hi, tools\\*, verification 2026-10-28, HCM_Wins_Under_Standard.txt.')

# ---------------------------------------------------------------- 5 mutants / research
head('5. MUTANTS AND RESEARCH BUILDS')
MUT = [
 ('lpaq1-h (lpaq1 + HCM record detector and contexts)', 'Canterbury 392,132 -> 359,786 B (7.17× -> 7.81×), decode 4.21 -> '
  '4.18 s; Silesia 1 MB sample 3,643,900 -> 3,559,695 B; Goodwin synthetic 8,113,745 B = smallest of all 14 tools '
  '(compress 146.8 s, decode 25.6 s); GCB 9,121,594 B (2.14×, smallest in the Mahoney race). Parallel 2 MB blocks: '
  'reymont decode 6.7 -> 4.1 s, x-ray 12.5 -> 6.5 s, dickens 12.4 -> 7.0 s at 1-6% size cost.'),
 ('HCM v3.7 (research, private)', 'HCM 3.6.1 + two kanzi-style contexts + kanzi EXE filter: ooffice 2,251,050 -> 1,857,184 B '
  '(3.31×); samba 3,355,530 -> 3,296,662 B; full Silesia max 41.74 MB (section 2 table).'),
 ('Sets reorder (setsort, private)', 'Silesia xml HCM 3.7 max 348,363 -> 341,969 B (15.63×), decode 4.91 -> 4.98 s, beats '
  'kanzi -l 9 342,833; 16 KB windows 338,588 B (15.79×). pcap 112,917,841 B: HCM 3.7 max 88,498,954 -> 80,616,598 B '
  '(1.40×), decode 117.4 -> 102.5 s; kanzi 88,755,437 -> 80,692,125; Brotli 11 89,598,288 -> 81,206,385; zstd -9 '
  '91,398,395 -> 82,701,553; zstd -3 92,254,442 -> 83,401,428; LZ4 HC -12 95,392,369 -> 87,638,425; bzip2 -9 '
  '92,613,792 -> 87,080,870; gzip -9 94,023,309 -> 92,702,776; BSC 86,687,460 -> 82,881,374.'),
 ('cascade (cascading lines; zstd build v1.1 and HCM build v3, private)', 'desktop: hi16_mix 876,307 -> 530,238 per million, '
  'dup_random_chunks 731,991 -> 431,312 per million (beats zstd --long 431,868 per million), random '
  'stored raw 1.00×; 3.2 GB tar 244,688 vs 244,338 per million (v1.1 now keeps the smaller). HCM cascade v3 (desktop, Sets fast '
  'off -> on): hi16 1.17× -> 1.93× (decode 1.76 -> 3.20 s), switch 1.76× -> 2.14× (4.65 -> 4.80 s), dup 1.39× '
  '-> 2.57× (5.04 -> 8.01 s).'),
 ('record delta front ends (Mutated)', 'kennedy.xls zstd -22 64,815 -> 26,398 B with 2.6x faster decode; x-ray zstd -19 '
  '5,129,823 -> 4,508,494 B, decode 24.6 -> 20.2 ms; zstd -1 x-ray 799,142 -> 592,974 per million; Snappy kennedy '
  '410,432 -> 126,015 per million (966 -> 717 us); LZFSE kennedy 132,346 -> 53,260 per million (1,064 -> 767 us). Silesia totals: section 2.'),
 ('change map (formerly activity map)', 'kennedy gzip -9 209,721 -> 35,558 B, decode 2,148 -> ~1,313-1,476 us; slower on '
  'lz4 (39,997 B, 739 -> ~1,000 us); Snappy x-ray 998,066 -> 611,453 per million but 11.4 vs 1.1 ms decode.'),
 ('Harrington Exchange (swap + juggle) front ends', 'synthetic packed 12-bit noise 324,507 B '
  '(1.62×; best known method 1.42×), decode 5.1 ms (zstd 0.89, xz 19.2 ms); NIST biased bits 19,722 B vs '
  'xz 21,772 B, decode 1.49 ms (xz 1.13 ms). On real images renaming hurts (x-ray 12-bit split-only 342,496 B is best).'),
 ('Parquet / ORC with record columns', 'DuckDB Parquet zstd: kennedy 158,437 -> 66,752 per million; x-ray 959,735 -> 624,848 per million '
  '(read 137 -> 102 ms); ORC zlib: x-ray 727,935 -> 554,086 per million (46.1 -> 25.6 ms); kennedy 257,999 -> 50,315 per million (2.8 -> 1.8 ms).'),
 ('hcm-trit, HCM-Bin, hcmx', 'Canterbury table in section 2; kennedy HCM-Bin v1 16,763 B (ZPAQ 16,822) decode 0.79 s vs '
  'ZPAQ 6.35 s; ptt5 27,623 B (ZPAQ 28,563) 0.26 s vs 3.29 s. Calgary hcmx max 707,392 B (4.60×), smallest in the '
  'Mahoney race.'),
 ('HCM parallel and Sets modes (v3.5 / v3.6)', 'Silesia samba v3.4 3,400,668 B (62.9 / 16.8 s) -> v3.5 blocks 3,443,819 '
  '(59.2 / 13.9 s) -> v3.6 Sets 3,406,084 (42.5 / 9.7 s); mozilla v3.5 13,412,925 (130.2 / 32.7 s). Decoder memory, '
  'Silesia desktop: single stream 86.8 s / 107 MB, blocks 16 MB 58.8 s / 230 MB, Sets 16 MB 47.3 s / 271 MB, Sets 4 MB '
  '26.6 s / 549 MB.'),
 ('IBM-style splitting controls (US8782019, US9792350) vs Sets, HCM fast, cloud', 'samba Sets 3,406,080 B (40.5 / 9.2 s) '
  'vs equal blocks 3,426,219 (62.3 / 9.4 s), IBM-A 3,536,707 (38.7 / 10.5 s), IBM-B 3,475,209 (57.7 / 12.6 s); mozilla '
  'Sets 13,419,755 (103.2 / 28.8 s) vs IBM-A 13,489,282 (94.4 / 24.9 s). Source ibm-comparison-2026-10-29\\results.txt.'),
 ('paq8px / cmix labs', 'paq8px lite model set ~2x faster (kennedy 414 -> 181 s) for 107,768 vs 107,450 per million on '
  'Canterbury; cmix run in 2.5-4 GB instead of 32 GB with no size change on fields.c (2,087 B).'),
 ('mzip research builds (AGPL, research only)', 'Silesia Sets + mzip 16 MB 4.84× decode 16.8 s vs mzip 4.92× 24.8 s '
  '(cloud); HX Complete + mzip 4.81× on 9 of 12 files. Section 2.')]
for nm, txt in MUT:
    w(); para(nm + ': ' + txt, '  ')
w(); w('  sources: memory /areas/hcm-benchmarks.md, /areas/combary.md, /areas/harringtons-sets.md; PC sets-cascade-2026-11-03,')
w('  setsort-reorder-2026-11-04, lpaq1-h, hcm37, hcm-v35/36, HCM-tests\\cascade-2026-11-03, tools-archive 2026-10-28.')

# ---------------------------------------------------------------- 6 task-level
head('6. TASK-LEVEL TESTS (Harrington\'s Sets applied to software that is not a compressor)')
w('  Speed-up = plain single run time / our run time on the same machine. "Identical" = output compared with the plain run.')
TASKS = [
 ('Game: autosave (gzip -6 of 131 MB save)', 'cloud 2c', '0.84 s', '0.45 s', '1.86x', 'yes (+0.031% size)', r'hs-games-2026-10-31\autosave'),
 ('Game: patch creation (xdelta3, 600 MB)', 'cloud 2c', '31.4 s', '19.1 s', '1.64x', 'yes (+721 B)', r'hs-games-2026-10-31\patch'),
 ('Game: patch apply (xdelta3)', 'cloud 2c', '1.31 s', '2.63 s', '0.50x', 'yes (loss)', r'hs-games-2026-10-31\patch'),
 ('Game: save-game load (131 MB XML)', 'cloud 2c', '0.701 s', '0.527 s', '1.33x', 'yes (771,200 obj.)', r'hs-games\savegame-load'),
 ('Game: sprite/texture load (3,020 PNG)', 'cloud 2c', '7.61 s', '3.91 s', '1.95x', 'yes (queue 4.03 s)', r'hs-games\sprites'),
 ('Game: LOD generation (30.6M tris)', 'cloud 2c', '31.03 s', '14.96 s', '2.07x', 'yes (= plain queue)', r'hs-games\lod'),
 ('Game: navmesh bake (Recast, 11,022 tiles)', 'cloud 2c', '18.4 s', '9.2 s', '2.0x', 'yes (queue 9.0 s)', r'hs-navmesh-2026-10-31'),
 ('Game: asset bundle load (zstd, 600 MB)', 'cloud 2c', '1.242 s', '0.415 s', '2.99x', 'chunks 3.65x faster', r'hs-games\asset-bundle'),
 ('Zeek 8.0.4, 903 MB pcap, 12 shards', 'desktop', '6.54 s', '4.35 s', '1.50x', 'yes, all 35,920 rows', r'security-tools\ZEEK-EXACT'),
 ('Zeek 8.0.4, 7.2 GB pcap, 12 shards', 'desktop', '50.34 s', '36.32 s', '1.39x', 'yes, 284,416 rows', r'security-tools\ZEEK-EXACT'),
 ('Zeek 8.0.4, 903 MB pcap, 2 shards', 'cloud 2c', '10.44 s', '8.12 s', '1.28x', '2 logs differed', r'security-tools\RESULTS'),
 ('Snort 3.12.4, 4 shards', 'cloud 2c', '8.91 s', '8.58 s', '1.04x', 'content alerts yes', r'security-tools\RESULTS'),
 ('ClamAV 1.5.1, 811 MB image, 4 pieces', 'cloud 2c', '162.1 s', '149.4 s', '1.08x', 'yes (detections)', r'security-tools\RESULTS'),
 ('binwalk 3.1.0, 2 pieces', 'cloud 2c', '2.72 s', '5.60 s', '0.49x', 'NO (loss)', r'security-tools\RESULTS'),
 ('YARA (hs_scan), 150 MB, 400 rules', 'cloud 2c', '10.8 s', '5.8 s', '1.86x', 'yes (offsets)', r'hs-scan-2026-10-30'),
 ('Wireshark tshark 4.6.9 (hs_shark direct)', 'desktop', '16.0 s', '5.6 s', '2.9x', 'yes, 964,896 lines', r'hs-shark-2026-10-30'),
 ('Tesseract OCR, 180 pages (hs_ocr)', 'desktop', '236.9 s', '35.4 s', '6.6x', 'yes (text bytes)', r'hs-ocr-2026-10-31'),
 ('whisper.cpp, 25.6 min audio (hs_whisper)', 'cloud 2c', '383.8 s', '331.4 s', '1.16x', 'WER 22.1 -> 17.1%', r'hs-whisper-2026-10-31'),
 ('Apache Tika + OCR, clean_mixed.pdf', 'desktop', '95.8 s', '16.2 s', '5.9x', 'yes (body text)', r'hs-tika-2026-10-31'),
 ('jq 1.7, 800 MB JSONL, summarize', 'cloud 2c', '30.8 s', '16.3 s', '1.88x', 'yes', r'hs-jq-2026-10-31'),
 ('jq 1.7 via hs_lines, 52 MB', 'cloud 2c', '3.65 s', '2.61 s', '1.40x', 'yes', r'hs-lines-2026-10-30'),
 ('river anomaly detector via hs_lines', 'cloud 2c', '27.4 s', '27.2 s', '1.01x', 'AUC .518 -> .652', r'hs-lines-2026-10-30'),
 ('GNU grep -o regex, 800 MB (hs_grep)', 'cloud 2c', '20.6 s', '10.7 s', '1.93x', 'yes (bytes)', r'hs-grep-2026-10-31'),
 ('Ghostscript render, 409 pages (hs_gs)', 'desktop', '56.5 s', '11.1 s', '5.1x', 'yes (page images)', r'hs-gs-2026-10-30'),
 ('Build: heavy C++ project (hs_build)', 'desktop', '78.7 s', '74.7 s', '1.05x', 'yes (objects)', r'hs-build-2026-10-31'),
 ('3D slicing, PrusaSlicer (first failure)', 'cloud 2c', 'n/a', 'n/a', 'none', 'no gain found', r'slicer-test-2026-10-31')]
w(f"  {'test':<42}{'machine':<9}{'plain':>9}{'ours':>9}{'speed':>7}  {'identical / note':<20}")
for a, b, c, d, e, f, g in TASKS:
    w(f"  {a[:41]:<42}{b:<9}{c:>9}{d:>9}{e:>7}  {f[:20]:<20}")
w(); w('  Sources by row (PC Documents\\data compression\\...):')
for a, *_, g in TASKS: w(f"    {a[:44]:<45} {g}")
w(); para('Notes: OpenSSL build 1.02-1.09x (cloud), none on desktop; ClamAV pieces cost 2.3x more total work; the game LOD, '
          'sprite and navmesh gains equal an ordinary shared work queue; whisper output is not byte-identical by design '
          '(word error rate shown). Compression-side Sets tests (hs_wrap, hs_zstd, HCM Sets) are in section 2 and 7.', '  ')

# ---------------------------------------------------------------- 7 our programs outside corpus tables
head('7. OUR PROGRAMS - OTHER RECORDED RESULTS (desktop speed records and early versions)')
OUR = [
 ('hs_zstd 1.0 (20 GB Silesia, desktop, 3 rounds)', 'decode 10.71 / 19.23 / 10.50 s vs zstd 19.46 / 28.72 / 28.51 s '
  '(safe claim 1.5-1.9x); compress 15.6 s vs zstd 68.2 s (1 core) / 18.8 s (-T0); 3.180× vs 3.183×.'),
 ('hs_zstd 1.1 vs pzstd, 10 GB (desktop)', 'decode pzstd 6.87 s vs hs_zstd 4 MB 4.00 s (1.72x).'),
 ('hs_zstd core scaling (4 GB Silesia, zstd -3, desktop)', 'decode at 1/2/4/6/8/12 jobs: hs_zstd 4 MB 7.52 / 4.87 / 3.24 / '
  '2.71 / 2.55 / 2.43 s; pzstd 4.77 / 4.04 / 3.60 / 3.78 / 3.49 / 3.53 s. Compress hs_zstd 19.59 -> 2.89 s (6.8x), '
  'pzstd 15.58 -> 3.78 s (4.1x).'),
 ('hs_zstd 1.3 ablation (16 MB blocks)', '4 types switching every 1 MB: 1.2 1.57×, plain blocks 1.71×, 1.3 1.88×, '
  'zstd -3 --long 1.71×; dup random chunks: 1.00× / 1.31× / 1.37× / 2.31×; Silesia joined 3.20× / 3.20× / '
  '3.20× / 3.23×.'),
 ('hs_wrap 1.5 + gzip, fair gzip baselines (4 GB Silesia, level 6, desktop)', 'decode gzip 14.16 s, pigz 9.55-9.77 s, '
  'rapidgzip 2.88 s, bgzip -@12 3.36 s (2.99×), hs_wrap 1.5 Sets 4.02 s (3.11×), plain blocks 4.03 s. rapidgzip '
  'beats hs_wrap + gzip on decode.'),
 ('hs_wrap 1.4 memory (2 GB, desktop)', '16 MB blocks + hold 14: decode 1.9 s at 276 MB; 8 MB + hold 14: 2.1 s at 165 MB; '
  'gzip alone 6.0 s at 9 MB.'),
 ('HCM v3 / v2 speed', 'v3 fast 3.96x compress / 4.10x decompress vs v2; memory fast 42 MB, max 68 MB, v2 121 MB.'),
 ('HCM v3.6 Windows, samba (desktop)', 'single 6.35× (33.4 / 8.1 s); blocks 6.27× (24.0 / 6.3 s); Sets 16 MB 6.34× '
  '(15.2 / 4.0 s); Sets 4 MB 6.19× (7.5 / 2.1 s).'),
 ('Early Python HCM (200 KB Python docs text)', 'context-balance coder 6.73× (29,725 B), ~27 s each way; restore '
  'point BWT route 5.65× (35,378 B, 0.9 s); ZPAQ -m5 7.12× (28,101 B, 1.0 s), bzip2 5.92×, xz 5.40×.'),
 ('H-Deflate v1 (4 deflate-heavy sets, cloud)', 'PNG 24 files 1.54× (dec 10.0 s) vs precomp route 1.23×; PDF 40 '
  'files 1.69× (3.89 s) vs 1.66×; Office 177 files 2.06× (5.22 s) vs 1.94×; archives 1.645× (15.3 s) vs '
  '1.645× (21.9 s). Full tables in section 2.')]
for nm, txt in OUR:
    w(); para(nm + ': ' + txt, '  ')
w(); w('  sources: memory /areas/combary.md and /areas/hcm-benchmarks.md; PC HCM-tests\\zstd20-2026-10-31,')
w('  pzstd-2026-10-31, hs-zstd-scaling-2026-11-01, hs-zstd-ablation-1.3, HCM-tests\\gzfair-2026-11-01, gzip-mem-2026-10-31,')
w('  hcm-v36-windows, h-deflate-2026-11-04\\H-Deflate-Results-2026-11-04.txt.')

# ---------------------------------------------------------------- 8 cmix / paq8px
head('8. cmix AND paq8px - Compared via existing metrics, not actually tested')
for txt in ['Silesia (211,938,580 B): published cmix v20 28,302,847 B = 7.49×. Our best measured: HCM v3.6 fast on the '
            'desktop 42,661,041 B = 4.97×; HCM v3.7 max (research) 4.06× file order; HX Complete 4.72× at 8.6 s decode. '
            'Compared via existing metrics, not actually tested.',
            'Silesia images: cmix mr 1,827,572 B (5.46×) vs HX 2,069,305 B (4.82×); cmix x-ray 3,503,868 B (2.42×) vs '
            'HX 3,651,990 B (2.32×). Compared via existing metrics, not actually tested.',
            'Random Turing machine data (19,552,881 B): predicted paq8px (2009) ~8.74 MB (2.24×) vs HX Complete 9,185,811 B '
            '(2.13×) and lpaq1 9,162,095 B (2.13×). Compared via existing metrics, not actually tested.',
            'Canterbury: cmix published 104,105 per million and paq8px v217 -8 107,450 per million; our '
            'best hcm-trit v2 max + dictionary 8.45×, hcmx max 8.08×. Compared via existing metrics, not actually tested.',
            'Biased files with a known floor: HX is within 0.06-0.31% of the floor (0.74% on the repeats file), so cmix or '
            'paq8px can be at most that much smaller there. Compared via existing metrics, not actually tested.',
            'Recorded exception: paq8px -1 / -6 was run once inside the hs_wrap speed matrix on 3 MB heads of 10 Silesia files '
            '(section 2, "Silesia 3 MB heads"), and the paq8px / cmix lab runs of 25 Oct (section 5). No full paq8px or cmix '
            'benchmark was run.']:
    w(); para(txt, '  ')
w(); w('  sources: plans\\All-Units-vs-All-Units-2026-11-04.txt section 12; HU-pretrain-results\\published\\; memory')
w('  /areas/combary.md (paq8px / cmix lab, cmix dictionary).')

# ---------------------------------------------------------------- 9 rankings
head('9. FINAL RANKINGS - every table, by ratio and separately by decode speed')
para('Only tools with an exact result on every file of the table are ranked; rows without a recorded decode time '
     'appear in the ratio list only. Marks: * ours, m modified, r mutant/research, blank original. Speed = decode MB/s '
     '(original bytes per second). Compress MB/s is in the CSV.', '  ')
CSVROWS = []
for t in sorted(TABLES, key=korder):
    if not t.rank: continue
    byr, byd = ranked(t)
    if len(byr) < 2: continue
    w(); para(f"[{t.key}] {t.title}")
    w(f"  {'#':>2} {'by ratio':<35} {'ratio':>7} | {'by decode speed':<35} {'dec MB/s':>8}")
    for i in range(max(len(byr), len(byd))):
        a = byr[i] if i < len(byr) else None; b = byd[i] if i < len(byd) else None
        sa = f"{GMARK[a.group]}{a.tool[:34]:<34} {ratio_s(a.ratio):>7}" if a else ' ' * 42
        sb = f"{GMARK[b.group]}{b.tool[:34]:<34} {num(b.dmb):>8}" if b else ''
        w(f"  {i + 1:>2} {sa} | {sb}")
    for r in byr:
        CSVROWS.append([t.key + ' | ' + t.title, r.tool, r.group, r.comp if r.comp else '', f"{r.ratio:.4f}",
                        f"{r.cmb:.3f}" if r.cmb else '', f"{r.dmb:.3f}" if r.dmb else '', t.source])

# absolute best per corpus
w(); rule('-'); w('ABSOLUTE BEST PER STANDARD CORPUS - everything we built or modified vs the best original program'); rule('-')
w('  (across all tables of a corpus; machines differ between tables, so each line names its machine)')
for key in ORDER + sorted({t.key for t in TABLES} - set(ORDER)):
    ts = [t for t in TABLES if t.key == key]
    if not ts: continue
    allr = [(r, t) for t in ts for r in t.rows if r.full and r.ratio]
    ours = [(r, t) for r, t in allr if r.group != G1]; oth = [(r, t) for r, t in allr if r.group == G1 and 'published' not in r.tool]
    if not ours: continue
    br = max(ours, key=lambda x: x[0].ratio); bo = max(oth, key=lambda x: x[0].ratio) if oth else None
    sp = [x for x in ours if x[0].dmb]; bs = max(sp, key=lambda x: x[0].dmb) if sp else None
    w(); w(f"  {key}")
    w(f"    best ratio, ours : {GMARK[br[0].group]}{br[0].tool[:44]:<44} {ratio_s(br[0].ratio):>7}  ({mshort(br[1])})")
    if bo: w(f"    best ratio, other: {bo[0].tool[:45]:<45} {ratio_s(bo[0].ratio):>7}  ({mshort(bo[1])})")
    if bs: w(f"    fastest ours     : {GMARK[bs[0].group]}{bs[0].tool[:36]:<36} {num(bs[0].dmb):>8} MB/s  {ratio_s(bs[0].ratio):>7}  ({mshort(bs[1])})")
w(); para('Showcase - exceptional results: HX Fast decodes x-ray at 642 MB/s on 12 threads (0.0132 s) at 2.32×, '
          'smaller than mzip, bzip3, BSC, kanzi and ZPAQ -m5; HX Complete is the smallest of every tool run on Calgary, '
          'Canterbury, Canterbury large and our synthetic set in the 3-4 Nov campaign while decoding faster than the '
          'runner-up; hcmx max is the smallest on Calgary in the Mahoney race (4.60×), zpaq-hi on Canterbury large '
          '(5.19×), lpaq1-h on the GCB (2.14×) and on Goodwin\'s synthetic set; HCM v3.4 max is the smallest of 14 '
          'tools on Canterbury (8.02×, decode 3.4 s vs ZPAQ -m5 11.5 s); HCM v3.6.1 is the smallest on the seismic, '
          'bank and plant data (plant: Sets mode 9.27×, 19% smaller than xz -9); the Sets reorder takes the 113 MB '
          'packet capture to 1.40×, the smallest of every tool run on it; HX lines sit within 0.06-0.31% of the '
          'entropy floor on structured biased data at 2-10 GB/s.', '  ')

# ---------------------------------------------------------------- 9b tier matrix
OUT_MX = sys.argv[4] if len(sys.argv) > 4 else None
BOUNDS = [2, 5, 10, 20, 50, 100, 200, 500, 2000, float('inf')]
def tier_of(m):
    for i, b in enumerate(BOUNDS):
        if m <= b: return i + 1
def is_hx(r): return 'HX' in r.tool and r.group == G2
MX = []; filled = both = native = rivals = 0
for nm, t in au_tables.items():
    lz = [r for r in t.rows if r.tool == 'LZ4 (default)' and r.full and r.ds]
    if not lz: continue
    lzd = lz[0].ds
    rows = [(r, r.ds / lzd) for r in t.rows if r.full and r.ratio and r.ds]
    for k in range(1, 11):
        hn = [(r, m) for r, m in rows if is_hx(r) and tier_of(m) == k]
        hx = [(r, m) for r, m in rows if is_hx(r) and tier_of(m) <= k]
        ot = [(r, m) for r, m in rows if r.group == G1 and tier_of(m) == k]
        h = max(hx, key=lambda x: x[0].ratio) if hx else None
        o = max(ot, key=lambda x: x[0].ratio) if ot else None
        if hn: native += 1
        if o: rivals += 1
        if hn or o: filled += 1
        if hn and o: both += 1
        MX.append((nm, k, h, o, bool(hn)))
ntypes = len({x[0] for x in MX})
head('9b. TYPE x SPEED-TIER MATRIX - HX vs the best other tool in every tier')
para(f"Cells: {ntypes} data types x 10 speed tiers = {ntypes * 10} cells. Filled (at least one HX or rival result "
     f"measured inside the tier): {filled}; not tested: {ntypes * 10 - filled}. HX measured inside the tier: {native} "
     f"cells; a rival measured inside the tier: {rivals}; both: {both}. Cells where HX only qualifies from a faster "
     'tier (rule 3d) show its entry with "^" and count as contested only if a rival is in the tier. The type list is '
     'the one master_report.py produces now: 29 types (its 3 Nov output had 26, the source of the 26 x 10 = 260 '
     'count). The tiers are the Compression Tier Standard v2.0 1-2-5 series (plans\\Compression-Tier-Standard-v2.0.txt; '
     'master_report.py itself defines no tiers): M = decode time / LZ4 default decode time on the same set; T1 M<=2, '
     'T2 <=5, T3 <=10, T4 <=20, T5 <=50, T6 <=100, T7 <=200, T8 <=500, T9 <=2,000, T10 >2,000.', '  ')
para('Rule used (as in Results-Summary-Kings-2026-11-04.txt): the HX entry is the smallest HX result whose M is in that '
     'tier or a faster one; the rival is the smallest original third-party tool whose M lands in that tier. Only '
     'full-coverage exact results count; HX research combinations (precomp, mzip, x264 routes) are left out. Data: the 3-4 Nov cloud campaign (section 2), the same data behind the '
     'HX-vs-best-other radar PNGs. Caveat from the Tier Standard: LZ4 was timed through its command line in this '
     "campaign, so slow tools' M is too low; a re-measurement will move some tools to slower tiers. Speeds are decode "
     'MB/s. Matrix CSV: tier-matrix-2026-11-06.csv.', '  ')
cur = None
for nm, k, h, o, nat in MX:
    if nm != cur:
        cur = nm; w(); w(f"  {nm[:96]}")
        w(f"   {'tier':<12}{'HX entry':<25}{'ratio':>7}{'MB/s':>8} | {'best other in tier':<23}{'ratio':>7}{'MB/s':>8}")
    rng = f"T{k} " + (f"M<={BOUNDS[k - 1]:,}" if k < 10 else 'M>2,000')
    if not h and not o: w(f"   {rng:<12}not tested"); continue
    hs = f"{(h[0].tool[:23] + ('' if nat else '^')):<25}{ratio_s(h[0].ratio):>7}{num(h[0].dmb):>8}" if h else f"{'none':<25}{'':>7}{'':>8}"
    os_ = f"{o[0].tool[:22]:<23}{ratio_s(o[0].ratio):>7}{num(o[0].dmb):>8}" if o else 'no rival measured in tier'
    w(f"   {rng:<12}{hs} | {os_}")
if OUT_MX:
    with open(OUT_MX, 'w', newline='', encoding='utf-8') as f:
        cw = csv.writer(f)
        cw.writerow(['data_type', 'tier', 'M_upper', 'hx_tool', 'hx_ratio', 'hx_decomp_MBps', 'hx_M', 'other_tool', 'other_ratio',
                     'other_decomp_MBps', 'other_M', 'status', 'source'])
        for nm, k, h, o, nat in MX:
            st = ('both' if nat and o else 'hx in tier' if nat else 'rival in tier, hx from faster tier' if (o and h) else
                  'rival only' if o else 'not tested (HX qualifies from a faster tier)' if h else 'not tested')
            cw.writerow([nm, k, BOUNDS[k - 1] if k < 10 else '', h[0].tool if h else '', f"{h[0].ratio:.4f}" if h else '',
                         f"{h[0].dmb:.3f}" if h else '', f"{h[1]:.2f}" if h else '', o[0].tool if o else '',
                         f"{o[0].ratio:.4f}" if o else '', f"{o[0].dmb:.3f}" if o else '', f"{o[1]:.2f}" if o else '', st,
                         SRC_AU + '; rivals\\rival3_results.csv'])

# ---------------------------------------------------------------- 10 gaps
head('10. GAPS - results with ratio but no speed, or speed but no ratio')
gaps_r, gaps_d = [], []
for t in TABLES:
    for r in t.rows:
        if r.ratio and not r.ds and not r.dmb: gaps_r.append(f"{r.tool} [{t.key}]")
        if not r.ratio and (r.ds or r.dmb): gaps_d.append(f"{r.tool} [{t.key}]")
w('  Ratio recorded, decode speed not recorded:')
for chunk in wrap('; '.join(sorted(set(gaps_r))), 94): w('    ' + chunk)
w(); w('  Speed recorded, ratio not recorded:')
for chunk in wrap('; '.join(sorted(set(gaps_d))) or 'none', 94): w('    ' + chunk)
w(); para('Compression time is not recorded for: the 27 Oct Tier-4 race, the enwik8 race, the 26-27 Oct record-delta '
          'and king-race totals, all Canterbury rows recorded only as ratios, zstd-hi / Brotli-hi, jxl-hi, the pzstd race summary '
          'and the Steim2 archive files. The per-slice build has no results yet. hx15 live run covers 10 of 30 listed '
          'unseen files.', '  ')
w(); rule(); w('END'); rule()

open(OUT_TXT, 'w', encoding='utf-8', newline='\r\n').write('\n'.join(L) + '\n')
with open(OUT_CSV, 'w', newline='', encoding='utf-8') as f:
    cw = csv.writer(f); cw.writerow(['corpus', 'tool', 'group', 'compressed_bytes', 'ratio', 'comp_MBps', 'decomp_MBps', 'source'])
    cw.writerows(CSVROWS)
print('wrapped', len(LONG)); [print('  W:', x) for x in LONG]
print('lines', len(L), 'csv rows', len(CSVROWS), 'tables', len(TABLES), 'tool entries', ntools)
