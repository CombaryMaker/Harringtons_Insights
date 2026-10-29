"""Every recorded result, per standard benchmark, gathered from the book tables (Harrington's Insights,
Chapters 4, 5, 6, 13, 15-18, Appendix A), the 29 Oct 2026 same-machine run (results.json) and extra_sets.json.
Entry: (name, kind, bytes, decode_seconds or None, source tag). No new measurements were made for this file.
kind: Standard / Original / Modified / Mutant / Best-of (per-file pick among our own tools).
"""
import json, os
HERE = os.path.dirname(os.path.abspath(__file__))

def fresh(setname):
    R = json.load(open(os.path.join(HERE, 'results.json')))
    rows = {v['tool']: v for v in R.values() if v['set'] == setname}
    out, perfile = [], {}
    for t, v in rows.items():
        f = v['files'].values()
        out.append((t, v['kind'], sum(x['comp'] for x in f), sum(x['dt'] for x in f), 'run'))
        perfile[t] = v
    # per-file best among our tools
    ours = [t for t, v in rows.items() if v['kind'] != 'Standard']
    std = [t for t, v in rows.items() if v['kind'] == 'Standard']
    files = list(next(iter(rows.values()))['files'])
    def pick(cands):
        tot, dt, chosen = 0, 0.0, {}
        for fn in files:
            b = min(cands, key=lambda t: rows[t]['files'][fn]['comp'])
            tot += rows[b]['files'][fn]['comp']; dt += rows[b]['files'][fn]['dt']; chosen[fn] = b
        return tot, dt, chosen
    tb, db, ch = pick(ours); out.append(('Harrington best-of (per file)', 'Best-of', tb, db, 'run'))
    ts, ds, chs = pick(std); out.append(('Best standard per file', 'Standard', ts, ds, 'run'))
    orig = sum(x['orig'] for x in next(iter(rows.values()))['files'].values())
    return orig, out, ch, chs

STANDARDS = {}

# ---------------- Canterbury corpus ----------------
o, e, ch, chs = fresh('Canterbury')
e += [  # book Chapter 5 (in-process decode), Chapter 4, Chapter 13
    ('LZMA -9 (SDK)', 'Standard', 493423, 0.0286, 'ch5'), ('lzma-hi', 'Modified', 486372, 0.0283, 'ch5'),
    ('7-Zip -mx9', 'Standard', 494902, 0.064, 'ch5'), ('7z-hi', 'Modified', 464853, 0.059, 'ch5'),
    ('zstd -22', 'Standard', 516237, None, 'ch5'), ('zstd-hi -22', 'Modified', 515628, None, 'ch5'),
    ('Brotli 11', 'Standard', 490662, None, 'ch5'), ('Brotli-hi 11', 'Modified', 486199, None, 'ch5'),
    ('zstd -22 + record delta', 'Mutant', 477733, None, 'ch4'),
    ('Snappy', 'Standard', 1264594, 0.0032, 'ch4'), ('Snappy + record delta', 'Mutant', 971717, 0.0029, 'ch4'),
    ('LZFSE', 'Standard', 670001, 0.0037, 'ch4'), ('LZFSE + record delta', 'Mutant', 588563, 0.0034, 'ch4'),
    ('hcmx max (best-of v3.3/trit2/Bin1)', 'Original', 347696, None, 'ch13'),
    ('paq8px -8', 'Standard', 302019, None, 'ch13'), ('cmix v21 (published)', 'Standard', 292617, None, 'ch13'),
]
STANDARDS['Canterbury corpus'] = dict(orig=o, entries=e, chosen=ch, std_chosen=chs,
    pairs=[('xz -9e', 'xz-hi'), ('bzip2 -9', 'bzip2-hi'), ('LZMA -9 (SDK)', 'lzma-hi'), ('7-Zip -mx9', '7z-hi'),
           ('Brotli 11', 'Brotli-hi 11'), ('zstd -22', 'zstd-hi -22'), ('zstd -22', 'zstd -22 + record delta'),
           ('Snappy', 'Snappy + record delta'), ('LZFSE', 'LZFSE + record delta'), ('lpaq1 -6', 'lpaq1-h'),
           ('ZPAQ -m5', 'HCM v3.4 max')])

# ---------------- Silesia corpus (full, 211,938,580 bytes) ----------------
SIL_FILES = ['dickens', 'mozilla', 'mr', 'nci', 'ooffice', 'osdb', 'reymont', 'samba', 'sao', 'webster', 'x-ray', 'xml']
SIL = {  # per-file (bytes, decode s)
 'HCM v3.3 max': [(2181812, 10.74), (13166396, 58.79), (2206541, 8.88), (1382358, 21.73), (2257148, 8.39), (2339520, 11.94),
                  (1063753, 6.41), (3362096, 20.70), (3898248, 10.97), (6271673, 43.18), (3734161, 10.32), (351427, 4.88)],
 'hcmx fast (best mode per file)': [(2214295, 11.8), (13140954, 89.3), (2209638, 11.8), (1388676, 20.7), (1978100, 11.6), (2333287, 16.6),
                  (1089243, 6.0), (3352638, 22.0), (3906120, 17.0), (6378584, 45.4), (3718336, 18.6), (343846, 4.5)],
 '7z-hi': [(2828100, .1494), (13165100, .9734), (2592100, .1611), (1444400, .1735), (2026300, .1366), (2839300, .192),
           (1314200, .0811), (3728100, .2752), (4408000, .2928), (8355900, .5369), (4197400, .2876), (434300, .0403)],
 'xz-hi': [(2825600, .1462), (13232900, 1.0229), (2594600, .1632), (1440100, .124), (2077700, .1433), (2835800, .1884),
           (1313900, .0821), (3728000, .254), (4417500, .323), (8346700, .4803), (4164500, .2923), (433900, .0304)],
 'zip-hi (7-Zip zip/PPMd, per-file)': [(2268508, 1.36), (13309321, 1.02), (2298976, .82), (1446220, .15), (2419567, .19), (2369785, 1.54),
           (1022713, .77), (3726508, .26), (4415023, .29), (6392189, 4.93), (3848954, 1.94), (378791, .33)],
}
SIL_STD = {
 'ZPAQ -m5': [(2094748, 26.05), (12041060, 136.65), (2181310, 19.41), (1251109, 79.02), (1766555, 16.70), (2204743, 28.76),
              (956504, 17.65), (3053823, 62.75), (3899259, 28.13), (5666837, 104.02), (3669704, 23.58), (326947, 11.49)],
 'ZPAQ -m4': [(2237879, 9.04), (13496587, 47.06), (2153628, 2.64), (1248417, 9.13), (1921973, 5.49), (2432037, 8.30),
              (1051244, 4.88), (3228824, 17.87), (4542517, 7.61), (6184237, 36.67), (3773047, 8.48), (357451, 3.60)],
 'BSC (max)': [(2259134, .58), (15907194, 4.15), (2206012, .56), (1210336, .53), (2559108, .64), (2224780, .68),
               (982364, .28), (3909928, 1.09), (4679764, 1.11), (6448532, 1.83), (3756584, .95), (393180, .12)],
 'bzip3': [(2233934, 1.39), (15881570, 6.15), (2119990, 1.35), (1425956, 1.79), (2526882, .87), (2261854, 1.38),
           (980459, .77), (3976068, 1.86), (4673307, 1.12), (6786980, 4.87), (3657083, 1.16), (405877, .38)],
}
# hcmx note: ooffice entry uses the in-format x86 filter (hcmx-s bin1-x86, 1,978,100 B, Appendix A.2)
def composite(table):
    tot, dt, chosen = 0, 0.0, {}
    for i, f in enumerate(SIL_FILES):
        n = min(table, key=lambda n: table[n][i][0]); tot += table[n][i][0]; dt += table[n][i][1]; chosen[f] = n
    return tot, dt, chosen
tb, db, chb = composite(SIL); ts, ds, chs = composite(SIL_STD)
e = [(n, 'Original' if n.startswith(('HCM', 'hcmx')) else 'Modified', sum(x[0] for x in v), sum(x[1] for x in v), 'ch5/ch13/A')
     for n, v in SIL.items()]
e[1] = ('hcmx fast (best mode per file)', 'Original', sum(x[0] for x in SIL['hcmx fast (best mode per file)']), sum(x[1] for x in SIL['hcmx fast (best mode per file)']), 'A.1/A.2')
e += [(n, 'Standard', sum(x[0] for x in v), sum(x[1] for x in v), 'ch13') for n, v in SIL_STD.items()]
e += [('Harrington best-of (per file)', 'Best-of', tb, db, 'composite'), ('Best standard per file', 'Standard', ts, ds, 'composite'),
      ('lz4', 'Standard', 100344651, 0.3, 'ch13'), ('BSC', 'Standard', 47428196, 8.3, 'ch13'), ('HCM v3.3 fast', 'Original', 42926135, 186.0, 'ch13'),
      ('LZMA -9 (SDK)', 'Standard', 48752900, 2.939, 'ch5'), ('lzma-hi', 'Modified', 48191700, 2.825, 'ch5'),
      ('xz -9e', 'Standard', 48455900, 3.306, 'ch5'), ('7-Zip -mx9', 'Standard', 48757500, 3.483, 'ch5'),
      ('RAR 7.30 -m5', 'Standard', 53129800, 1.059, 'ch5'), ('RAR + detector delta', 'Modified', 52171400, 1.019, 'ch5'),
      ('gzip -9', 'Standard', 67766000, None, 'ch4'), ('gzip -9 + record delta', 'Mutant', 66464000, None, 'ch4'),
      ('zstd -19', 'Standard', 52892000, None, 'ch4'), ('zstd -19 + record delta', 'Mutant', 52568000, None, 'ch4'),
      ('xz -9e + record delta', 'Mutant', 48075000, None, 'ch4')]
STANDARDS['Silesia corpus (full)'] = dict(orig=211938580, entries=e, chosen=chb, std_chosen=chs,
    pairs=[('xz -9e', 'xz-hi'), ('LZMA -9 (SDK)', 'lzma-hi'), ('7-Zip -mx9', '7z-hi'), ('RAR 7.30 -m5', 'RAR + detector delta'),
           ('gzip -9', 'gzip -9 + record delta'), ('zstd -19', 'zstd -19 + record delta'), ('xz -9e', 'xz -9e + record delta'),
           ('ZPAQ -m4', 'HCM v3.3 max')])

# ---------------- Goodwin sets (same-machine run) ----------------
for s, label in (('Silesia 1 MB sample', 'Silesia 1 MB sample (Goodwin)'), ('Synthetic (Goodwin)', 'Synthetic set (Goodwin)')):
    o, e, ch, chs = fresh(s)
    STANDARDS[label] = dict(orig=o, entries=e, chosen=ch, std_chosen=chs,
        pairs=[('xz -9e', 'xz-hi'), ('bzip2 -9', 'bzip2-hi'), ('lpaq1 -6', 'lpaq1-h'), ('ZPAQ -m5', 'HCM v3.4 max'), ('ZPAQ -m5', 'HCM v3.4 fast')])

# ---------------- Biased packed data and radio (extra_sets.json) ----------------
X = json.load(open(os.path.join(HERE, 'extra_sets.json')))
b = X['Biased packed data (4 files)']
e = [(n, t['kind'], t['comp'], (t['dt_ms'] / 1000 if t.get('dt_ms') else None), 'ch15') for n, t in b['tools'].items()]
pf = {n: t['per_file'] for n, t in b['tools'].items() if 'per_file' in t}
ours = [n for n in pf if b['tools'][n]['kind'] != 'Standard']
best = [min(ours, key=lambda n: pf[n][i]) for i in range(4)]
e.append(('Harrington best-of (per file)', 'Best-of', sum(pf[n][i] for i, n in enumerate(best)),
          sum(b['tools'][n]['per_file_dt'][i] for i, n in enumerate(best)) / 1000, 'composite'))
STANDARDS['Biased packed data'] = dict(orig=b['orig'], entries=e, chosen=dict(zip(b['files'], best)),
    pairs=[('Blosc2 (best)', 'Exchange->Blosc2 hybrid'), ('zstd -19', 'Harrington Exchange')])
r = X['Radio SDR captures (6 files)']
e = [(n, t['kind'], t['comp'], (t['dt_ms'] / 1000 if t.get('dt_ms') else None), 'ch15') for n, t in r['tools'].items()]
STANDARDS['Radio SDR captures'] = dict(orig=r['orig'], entries=e, pairs=[('xz -9e', 'xz-hi'), ('xz -9e', 'Exchange->xz')])

# ---------------- Modern log and export data (Chapter 18, 66,124,774 bytes; decode = total CPU) ----------------
STANDARDS['Modern logs and exports'] = dict(orig=66124774, entries=[
    ('gzip -9', 'Standard', 11599301, .384, 'ch18'), ('bzip2 -9', 'Standard', 7357306, 1.942, 'ch18'),
    ('xz -9e', 'Standard', 4053776, .384, 'ch18'), ('zstd -19', 'Standard', 4414496, .111, 'ch18'),
    ('zstd -22', 'Standard', 4415744, .132, 'ch18'), ('lz4 -12', 'Standard', 13661665, .125, 'ch18'),
    ('HCM fast', 'Original', 4426188, 64.8, 'ch18'), ('HCM max', 'Original', 4340977, 75.2, 'ch18'),
    ('zstd-hi', 'Modified', 4409210, .122, 'ch18'), ('xz-hi (smallest)', 'Modified', 4013900, .390, 'ch18'),
    ('xz-hi (No-Pain pick)', 'Modified', 4040884, .360, 'ch18'),
    ], pairs=[('xz -9e', 'xz-hi (No-Pain pick)'), ('xz -9e', 'xz-hi (smallest)'), ('zstd -22', 'zstd-hi')],
    note='HCM max is the smallest on the four log files (hdfs 42,132 -> 31,068 bytes vs the best stock tool, openstack 38,656 -> 30,192, apache 118,072 -> 80,254, nginx.json 228,132 -> 135,706) but loses on the large exports.')

# ---------------- PDF set (Chapter 6; time = render of first 3 pages) ----------------
STANDARDS['PDF documents (139 files)'] = dict(orig=95872155, entries=[
    ('Original files', 'Standard', 95872155, 11.22, 'ch6'), ('qpdf optimisation', 'Standard', 81681005, 11.35, 'ch6'),
    ('Ours, No-Pain pick', 'Modified', 81173302, 11.20, 'ch6'), ('Ours, smallest always', 'Modified', 80277600, 11.60, 'ch6')],
    pairs=[('qpdf optimisation', 'Ours, No-Pain pick')], time_label='Render time, first 3 pages (s)')

# ---------------- Speech audio (FLAC) ----------------
STANDARDS['Speech audio (FLAC, 6 speakers)'] = dict(orig=20997112, entries=[
    ('flac -8', 'Standard', 11162773, .136, 'ch16'), ('flac-hi (No-Pain)', 'Modified', 10924361, .123, 'ch16')],
    pairs=[('flac -8', 'flac-hi (No-Pain)')])

# ---------------- spotlights ----------------
KENNEDY = [  # (codec, plain bytes, plain decode us, ours label, ours bytes, ours decode us)
    ('zstd -22', 64818, 1359, 'record delta', 26401, 520), ('gzip -9', 209721, 2179, 'record delta', 42971, 1054),
    ('gzip -9', 209721, 2148, 'activity map', 35558, 1400), ('LZ4 -12', 324136, 770, 'record delta', 43653, 725),
    ('Snappy', 422640, 966, 'record delta', 129763, 717), ('LZFSE', 136282, 1064, 'record delta', 54844, 767),
    ('xz -9e', 51868, 6480, 'xz-hi (stock xz)', 23656, 2900), ('7-Zip -mx9', 50529, 9900, '7z-hi (Delta 13)', 23764, 6500), ('ZPAQ -m5', 16822, 6350000, 'HCM-Bin (hcmx max)', 16797, 1202000),
    ('lpaq1 -6', 37943, None, 'lpaq1-h', 21644, None)]
XRAY = [  # x-ray (8,474,240 B), stock vs ours with decode ms
    ('zstd -19', 5129823, 24.6, 'record delta + zstd -19 (8-run median)', 4508494, 20.2),
    ('ORC zlib, 1 column', 6168696, 46.1, 'ORC zlib, detected columns', 4695458, 25.6),
    ('ORC zstd, 1 column', 6855465, 13.6, 'ORC zstd, detected columns', 4703390, 10.2),
    ('Parquet zstd, 1 column', 8133025, 137, 'Parquet zstd, detected columns', 5295112, 102),
    ('JPEG XL e7', 3631359, None, 'jxl-hi -P15 -g1 -I100 (1.35x faster)', 3547061, None),
    ('7-Zip -mx9', 4479900, 315.9, '7z-hi (Delta 2)', 4197400, 287.6)]
JXL = [  # image, size change %, decode speed-up x, award
    ('x-ray', +0.3, 5.31, 'BGS-2'), ('x-ray', -2.3, 1.35, 'BGS-1, smaller'), ('RG1 radiograph', +2.9, 7.48, 'BGS-2'),
    ('RG1 radiograph', -2.2, 1.04, 'No-Pain'), ('MR2', +3.6, 4.83, 'BGS-2'), ('RG3', -0.5, 1.30, 'BGS-1, smaller'),
    ('OBXXXX1A ultrasound', +4.8, 1.37, 'BGS-1'), ('mr', -0.1, 1.06, 'No-Pain')]

# not exceptional (listed in the scoreboard for completeness)
MINOR = ['Kodak photos, WebP lossless: 11,204,040 -> 11,202,034 bytes (0.018%), decode 262 -> 258 ms.',
         'enwik8: HCM max 21,701,018 bytes vs ZPAQ -m5 19,625,015 and BSC max 21,480,530 (5.98 s decode vs 115 s) - HCM loses here.']
