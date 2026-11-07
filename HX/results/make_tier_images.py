#!/usr/bin/env python3
"""make_tier_images.py - one image per speed tier: the 29 data types with the best and second-best tool in that tier.
(c) 2026 Michael Hugh Harrington. usage: make_tier_images.py final-rankings.csv tier-matrix.csv OUTDIR
Same data and rules as make_top_performers.py (same-machine campaign 3-4 Nov 2026; Compression Tier Standard v2.0:
M = LZ4 decode speed / tool decode speed; a tool counts in every tier whose limit it meets)."""
import sys, csv, glob, collections, os
from PIL import Image, ImageDraw, ImageFont
rank, mat, outdir = sys.argv[1:4]; os.makedirs(outdir, exist_ok=True)
EXCL = ('precomp', 'mzip', 'x264'); OURS = {'ours', 'modified', 'mutant/research'}
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
    v = dict(tool=r['tool'], ours=r['group'] in OURS, ratio=float(r['ratio']), dec=float(r['decomp_MBps']))
    if r['tool'] not in rows[t] or v['ratio'] > rows[t][r['tool']]['ratio']: rows[t][r['tool']] = v
SHORT = {'Biased test files, 8 MB each': 'Biased data', 'NIST SP 800-90B real noise samples': 'NIST real noise',
 'Random Turing machine output (uiq2, Generic Compression Benchmark data), 3 seeds': 'Random Turing output (GCB)',
 'Text: server logs (16 systems), Chinese poetry JSON, CSV data, C++ source tree (TAR)': 'Text, logs, JSON, CSV, code',
 'Programs: Windows x64 EXE (zstd, pzstd, bgzip, distlib) and Linux x86-64 (python, libc, libstdc++, cc1plus, git)': 'Programs (Windows, Linux)',
 'Silesia, 12 files': 'Silesia corpus', 'Calgary corpus, 18 files': 'Calgary corpus', 'Canterbury corpus, 11 files': 'Canterbury corpus',
 'Canterbury large corpus, 3 files': 'Canterbury large', 'Synthetic test files, 5 files': 'Synthetic files',
 'Speech, WAVs, 8 kHz mono (FSDD)': 'Speech audio', 'Music and sounds, WAVs, 44.1 kHz stereo, decoded from Ogg Vorbis (librosa data)': 'Music audio',
 'Environmental recordings, WAVs, 44.1 kHz mono (ESC-50)': 'Field recordings', 'Already-compressed audio files: MP3, AAC, Opus, Ogg, FLAC': 'MP3 / AAC / FLAC files',
 'Colour photos, raw RGB (Kodak 24, PPM)': 'Raw colour photos', 'Rendered document pages / screenshots, raw (PGM/PPM)': 'Screenshots / pages',
 'Medical DICOM files (imageio test images)': 'Medical DICOM', 'PNG files (Kodak 24)': 'PNG files', 'JPEG photos': 'JPEG photos',
 'GIF and TIFF files': 'GIF and TIFF', 'Raw video (Y4M; frames decoded from H.264 sources + one raw test clip)': 'Raw video',
 'Compressed video files (H.264, HEVC, VP9)': 'Compressed video', 'PDF files from the pdf.js test set': 'PDF files',
 'Packet captures, public pcapng files': 'Packet captures (pcapng)', 'Packet capture, one 113 MB merged pcap': 'Packet capture, 113 MB',
 'Office files: DOCX/XLSX/PPTX/DOC/XLS/PPT (Apache POI test data)': 'Office files', 'SQLite database (Chinook)': 'SQLite database',
 'Archives: ZIP, TAR, TAR.GZ': 'Archives (ZIP, TAR)', 'Scientific: climate NetCDF/GRIB, radar, ice velocity, 3D models (OBJ), genomics (SAM, FASTA, E.coli), FITS, NumPy': 'Scientific data'}
def short_tool(s):
    for a, b in ((' (official, max)', ' max'), (' (official)', ''), ('(Sets, raw off, safe), ', ''), (' (default)', ''), ('7-Zip -mx9 (LZMA2)', '7-Zip LZMA2'),
                 ('7-Zip PPMd (-mx9 -m0=PPMd)', '7-Zip PPMd'), ('adaptive arithmetic (ac0)', 'arithmetic coder'), (' blocks', '')):
        s = s.replace(a, b)
    return s
font = lambda n: glob.glob(f'/usr/share/fonts/**/{n}', recursive=True)[0]
R, RB, RI = font('DejaVuSans.ttf'), font('DejaVuSans-Bold.ttf'), font('DejaVuSans-Oblique.ttf')
K = 2; F = lambda p, s: ImageFont.truetype(p, s * K)
W, TOPH, RH, FOOT = 1200, 230, 40, 92
NAVY, GREEN, GREENBG, GREY, LINE, ZEBRA = (26, 61, 109), (27, 110, 52), (228, 243, 230), (95, 105, 115), (205, 212, 220), (246, 248, 251)
def fit(d, text, f, w):
    if d.textlength(text, font=f) <= w: return text
    while text and d.textlength(text + '…', font=f) > w: text = text[:-1]
    return text + '…'
counts = {}
for k in sorted(upper):
    u = upper[k]; H = TOPH + RH * (len(types) + 1) + FOOT
    img = Image.new('RGB', (W * K, H * K), 'white'); d = ImageDraw.Draw(img); s = lambda v: int(v * K)
    d.rectangle([0, 0, s(W), s(150)], fill=NAVY)
    d.text((s(48), s(26)), f'Speed Tier {k}', font=F(RB, 66), fill='white')
    sub = f'M ≤ {u:g}' if u else 'Any speed'
    d.text((s(W - 48), s(36)), sub, font=F(RB, 44), fill=(255, 214, 102), anchor='ra')
    d.text((s(W - 48), s(96)), f'≤ {u:g}× LZ4’s decode time' if u else 'size only, no speed limit', font=F(R, 20), fill=(220, 230, 245), anchor='ra')
    first = sum(1 for t in types if t in rows and t in ref and (lambda ok: ok and ok[0]['ours'])(sorted([v for v in rows[t].values() if u is None or ref[t] / v['dec'] <= u], key=lambda v: -v['ratio'])))
    d.text((s(48), s(170)), f'Michael Harrington’s software is 1st in {{n}} of 29 data types'.replace('{n}', str(first)), font=F(RB, 22), fill=GREEN)
    d.text((s(48), s(200)), 'Ratio = original size ÷ compressed size. Green = my software. Every file checked byte for byte.', font=F(R, 15), fill=GREY)
    cols = [(48, 'Data type'), (400, '1st'), (800, '2nd')]
    y0 = TOPH; d.rectangle([s(36), s(y0), s(W - 36), s(y0 + RH)], fill=(221, 230, 242))
    for x, h in cols: d.text((s(x), s(y0 + 10)), h, font=F(RB, 17), fill=NAVY)
    for i, t in enumerate(types):
        y = y0 + RH * (i + 1)
        if i % 2: d.rectangle([s(36), s(y), s(W - 36), s(y + RH)], fill=ZEBRA)
        d.line([s(36), s(y), s(W - 36), s(y)], fill=LINE, width=K)
        d.text((s(48), s(y + 10)), SHORT.get(t, t), font=F(R, 16), fill=(30, 40, 48))
        ok = sorted([v for v in rows[t].values() if u is None or ref[t] / v['dec'] <= u], key=lambda v: -v['ratio']) if t in rows and t in ref else []
        for j, (x, cw) in enumerate(((400, 370), (800, 352))):
            v = ok[j] if j < len(ok) else None
            if not v: d.text((s(x), s(y + 10)), 'no tool this fast', font=F(RI, 15), fill=(150, 150, 150)); continue
            if v['ours']: d.rectangle([s(x - 10), s(y + 3), s(x + cw + 8), s(y + RH - 3)], fill=GREENBG)
            fb = F(RB if v['ours'] else R, 16); rt = f"{v['ratio']:.2f}×"
            rw = d.textlength(rt, font=F(RB, 16)) / K
            d.text((s(x), s(y + 10)), fit(d, short_tool(v['tool']), fb, s(cw - rw - 14)), font=fb, fill=GREEN if v['ours'] else (30, 40, 48))
            d.text((s(x + cw), s(y + 10)), rt, font=F(RB, 16), fill=GREEN if v['ours'] else NAVY, anchor='ra')
    yb = y0 + RH * (len(types) + 1)
    d.line([s(36), s(yb), s(W - 36), s(yb)], fill=LINE, width=K)
    d.text((s(48), s(yb + 22)), 'M = how many times longer than LZ4 a tool takes to decode the same data. A tool counts in its own tier and every slower one.', font=F(R, 14), fill=GREY)
    d.text((s(48), s(yb + 48)), 'Michael Hugh Harrington · HX is Patent Pending (U.S. Provisional App. 64/192,843) · Same machine, same session, 3-4 Nov 2026', font=F(R, 14), fill=GREY)
    img.save(os.path.join(outdir, f'Speed-Tier-{k:02d}.png'), optimize=True); counts[k] = first
print(counts)
