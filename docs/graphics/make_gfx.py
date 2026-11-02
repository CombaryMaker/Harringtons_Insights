# make_gfx.py - ten cartoon SVG illustrations for michaelharrington.com/compression.html
# Michael Hugh Harrington, 2026. MIT License.
# Flat cartoon style: thick navy outlines, soft drop shadows, bright fills, rounded shapes.
import math, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'svg'); os.makedirs(OUT, exist_ok=True)
INK = '#22304A'; CORAL = '#FF7A59'; SUN = '#FFC93C'; TEAL = '#2EC4B6'; SKY = '#6EC1FF'; GRAPE = '#9B7BFF'
LEAF = '#7BD389'; PAPER = '#FFFFFF'; STEEL = '#C9D6E8'; DARKSTEEL = '#8EA3C0'; PINK = '#FF9EC4'
FONT = "'Trebuchet MS', 'Arial Rounded MT Bold', 'Segoe UI', Arial, sans-serif"
W, H = 800, 460

def svg(title, bg, body, caption):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" aria-label="{title}. {caption}">
<title>{title}</title>
<defs>
  <filter id="soft" x="-10%" y="-10%" width="130%" height="140%"><feDropShadow dx="0" dy="5" stdDeviation="0" flood-color="{INK}" flood-opacity="0.18"/></filter>
</defs>
<rect x="0" y="0" width="{W}" height="{H}" rx="28" fill="{bg}"/>
<g stroke="{INK}" stroke-width="4" stroke-linejoin="round" stroke-linecap="round" font-family="{FONT}">
<text x="40" y="58" font-size="30" font-weight="700" fill="{INK}" stroke="none">{title}</text>
{body}
<text x="{W/2}" y="{H-24}" font-size="19" fill="{INK}" stroke="none" text-anchor="middle">{caption}</text>
</g>
</svg>
'''

def rect(x, y, w, h, fill, r=12, sw=4, extra=''):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke-width="{sw}" {extra}/>'
def circ(cx, cy, r, fill, sw=4):
    return f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" stroke-width="{sw}"/>'
def poly(pts, fill, sw=4):
    return '<polygon points="' + ' '.join(f'{x:.1f},{y:.1f}' for x, y in pts) + f'" fill="{fill}" stroke-width="{sw}"/>'
def line(x1, y1, x2, y2, sw=4, color=INK, dash=''):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{sw}"{d}/>'
def path(d, fill='none', sw=4, extra=''):
    return f'<path d="{d}" fill="{fill}" stroke-width="{sw}" {extra}/>'
def text(x, y, s, size=18, anchor='middle', weight='700', fill=INK):
    return f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" fill="{fill}" stroke="none" text-anchor="{anchor}">{s}</text>'
def g(content, extra=''):
    return f'<g {extra}>{content}</g>'

# --- data pieces: four "kinds" of data -------------------------------------------------------------------
def piece(kind, cx, cy, s=14):
    if kind == 'c': return circ(cx, cy, s, CORAL, 3)
    if kind == 's': return rect(cx - s, cy - s, 2 * s, 2 * s, SKY, 4, 3)
    if kind == 't': return poly([(cx, cy - s * 1.15), (cx + s * 1.1, cy + s * 0.85), (cx - s * 1.1, cy + s * 0.85)], SUN, 3)
    if kind == 'h':   # hexagon
        return poly([(cx + s * 1.05 * math.cos(a), cy + s * 1.05 * math.sin(a)) for a in [math.pi / 3 * i for i in range(6)]], LEAF, 3)
    if kind == 'x':   # scribble noise pebble (incompressible)
        return circ(cx, cy, s * 0.8, DARKSTEEL, 3) + path(f'M{cx-s*0.5},{cy} q{s*0.25},{-s*0.5} {s*0.5},0 t{s*0.5},0', sw=2.5)
    raise ValueError(kind)

def gear(cx, cy, r, fill, teeth=10, sw=4):
    pts = []
    for i in range(teeth * 2):
        a = math.pi * i / teeth; rr = r if i % 2 == 0 else r * 0.78
        a1, a2 = a - math.pi / teeth * 0.45, a + math.pi / teeth * 0.45
        pts += [(cx + rr * math.cos(a1), cy + rr * math.sin(a1)), (cx + rr * math.cos(a2), cy + rr * math.sin(a2))]
    return poly(pts, fill, sw) + circ(cx, cy, r * 0.3, PAPER, sw)

def eyes(cx, cy, gap=26, r=9):
    return (circ(cx - gap / 2, cy, r, PAPER, 3) + circ(cx + gap / 2, cy, r, PAPER, 3)
            + f'<circle cx="{cx - gap/2 + 2}" cy="{cy + 1}" r="{r*0.45}" fill="{INK}" stroke="none"/>'
            + f'<circle cx="{cx + gap/2 + 2}" cy="{cy + 1}" r="{r*0.45}" fill="{INK}" stroke="none"/>')
def smile(cx, cy, w=22):
    return path(f'M{cx-w/2},{cy} q{w/2},{w*0.55} {w},0', sw=3.5)

def conveyor(x, y, w, h=22, fill=STEEL):
    s = rect(x, y, w, h, fill, h / 2)
    n = max(2, int(w // 44))
    for i in range(n):
        s += circ(x + h / 2 + i * (w - h) / (n - 1), y + h / 2, 6, PAPER, 3)
    return s

def arrow(x1, y1, x2, y2, color=INK, sw=5):
    a = math.atan2(y2 - y1, x2 - x1); L = 14
    p1 = (x2 - L * math.cos(a - 0.5), y2 - L * math.sin(a - 0.5)); p2 = (x2 - L * math.cos(a + 0.5), y2 - L * math.sin(a + 0.5))
    return line(x1, y1, x2, y2, sw, color) + poly([(x2, y2), p1, p2], color, 2)

files = {}

# 1. The Sorting Machine ---------------------------------------------------------------------------------
b = ''
# hopper with mixed pieces
b += poly([(40, 110), (230, 110), (175, 205), (95, 205)], SUN)
mixed = 'cstchsctshcthsct'
for i, k in enumerate(mixed[:12]):
    b += piece(k, 70 + (i % 6) * 26, 130 + (i // 6) * 28, 10)
b += rect(110, 205, 50, 34, DARKSTEEL, 6)
# chute into machine
b += conveyor(70, 262, 180)
for i, k in enumerate('chst'):
    b += piece(k, 95 + i * 40, 246, 11)
# machine body
b += g(rect(255, 120, 230, 220, TEAL, 26), 'filter="url(#soft)"')
b += rect(280, 140, 180, 70, PAPER, 14) + eyes(370, 172, 46, 13) + smile(370, 196, 30)
b += gear(305, 270, 30, SUN) + gear(360, 292, 22, CORAL, 8) + gear(425, 262, 34, GRAPE, 11)
b += text(370, 330, 'SORTER', 16)
# lanes out
lanes = [('c', 165), ('s', 228), ('t', 291), ('h', 354)]
for k, y in lanes:
    b += conveyor(500, y - 11, 170, 22)
    for j in range(3):
        b += piece(k, 525 + j * 45, y - 28, 11)
    b += g(rect(680, y - 34, 74, 50, PAPER, 10), 'filter="url(#soft)"') + text(717, y - 3, 'core', 15, weight='700')
b += text(717, 100, 'cores', 15, weight='400')
files['01-sorting-machine'] = svg("The Sorting Machine", '#E8F6FF', b,
    "Harrington's Sets: like goes with like, then every core gets its own lane.")

# 2. One Lane or Twelve ----------------------------------------------------------------------------------
b = ''
def car(x, y, col):
    return (rect(x, y, 46, 24, col, 9, 3) + rect(x + 9, y - 13, 26, 15, PAPER, 6, 3)
            + circ(x + 11, y + 26, 6, INK, 2) + circ(x + 35, y + 26, 6, INK, 2))
def booth(x, y, open_=True):
    return (rect(x, y, 34, 44, SUN if open_ else STEEL, 6, 3) + rect(x + 6, y + 8, 22, 14, PAPER, 3, 3)
            + line(x + 34, y + 30, x + 64, y + (30 if not open_ else 12), 4, CORAL))
cols = [CORAL, SKY, GRAPE, LEAF, SUN, PINK]
# top: one lane, long queue
b += text(40, 98, 'One core', 20, 'start')
b += rect(40, 112, 720, 70, '#D7E3F2', 14, 3)
b += booth(640, 120)
for i in range(11):
    b += car(60 + i * 52, 140, cols[i % 6])
b += text(700, 104, 'waiting...', 15, weight='400')
# bottom: many lanes
b += text(40, 222, 'Twelve cores, same cars, same order out', 20, 'start')
for r in range(4):
    y = 232 + r * 46
    b += rect(40, y, 720, 44, '#D7E3F2', 10, 3)
    b += booth(640, y)
    for i in range(3):
        b += car(120 + i * 150 + r * 25, y + 14, cols[(r + i) % 6])
b += text(760, 222, '(+8 more lanes)', 14, 'end', '400')
files['02-one-lane-or-twelve'] = svg("One Lane or Twelve", '#F1F7EC', b,
    "Same work, same results: every lane open instead of one.")

# 3. The Coat-Check Tracker ------------------------------------------------------------------------------
b = ''
order = ['c', 's', 't', 'h', 'c', 't']
# incoming row with tags
b += text(40, 104, 'In', 20, 'start')
for i, k in enumerate(order):
    x = 90 + i * 70
    b += piece(k, x, 140, 18)
    b += rect(x - 15, 168, 30, 24, PAPER, 5, 3) + text(x, 186, str(i + 1), 15)
    b += line(x, 160, x, 168, 3)
# counter
b += g(rect(520, 96, 240, 120, SUN, 18), 'filter="url(#soft)"') + text(640, 140, 'TRACKER', 22)
b += rect(545, 152, 190, 46, PAPER, 10, 3) + text(640, 182, '1 2 3 4 5 6', 20, weight='400')
# shuffled middle (worked on in groups)
b += text(40, 262, 'Worked on by type', 18, 'start', '400')
groups = [[('c', 1), ('c', 5)], [('t', 3), ('t', 6)], [('s', 2)], [('h', 4)]]
x = 90
for grp in groups:
    gx0 = x - 26
    b += rect(gx0, 276, 52 + (len(grp) - 1) * 60, 64, PAPER, 14, 3)
    for k, n in grp:
        b += piece(k, x, 300, 14) + text(x, 334, str(n), 13)
        x += 60
    x += 30
# out row restored
b += text(565, 262, 'Out, same order', 18, 'start', '400')
for i, k in enumerate(order):
    xx = 575 + i * 32
    b += piece(k, xx, 304, 11) + text(xx, 334, str(i + 1), 13)
b += arrow(516, 308, 552, 308, TEAL)
files['03-coat-check-tracker'] = svg("The Coat-Check Tracker", '#FFF4E8', b,
    "A small tracker remembers where every piece belongs, so nothing is ever lost.")

# 4. Finding the Row Length ------------------------------------------------------------------------------
b = ''
row = [CORAL, SKY, SUN, LEAF, GRAPE]
b += text(40, 104, 'A long ribbon of data...', 18, 'start', '400')
for i in range(12):
    b += rect(40 + i * 35, 116, 33, 33, row[i % 5], 4, 3)
b += text(468, 140, '...', 22)
# ruler measuring 5
b += rect(40, 160, 175, 26, SUN, 6, 3)
for t in range(6):
    b += line(40 + t * 35, 160, 40 + t * 35, 172, 3)
b += text(127, 206, 'row = 5 wide', 16)
# magnifier
# arrow to grid
b += arrow(300, 240, 470, 240, TEAL) + text(385, 228, 'fold every 5', 15, weight='400')
b += text(500, 104, '...folded into rows', 18, 'start', '400')
for r in range(4):
    for c in range(5):
        b += rect(500 + c * 50, 120 + r * 50, 46, 46, row[c], 6, 3)
# column highlight
b += rect(494, 114, 58, 206, 'none', 10, 4, f'stroke="{TEAL}" stroke-dasharray="10 8"')
b += text(625, 350, 'Columns line up, so the pattern stacks', 16, weight='400')
files['04-finding-the-row-length'] = svg("Finding the Row Length", '#F3EEFF', b,
    "Record detection: measure the row, line the rows up, and the repeats compress easily.")

# 5. Checked to the Last Row -----------------------------------------------------------------------------
b = ''
def receipt(x, title, col):
    s = g(rect(x, 96, 200, 270, PAPER, 10), 'filter="url(#soft)"') + rect(x, 96, 200, 44, col, 10, 4) + text(x + 100, 126, title, 18)
    for i in range(9):
        y = 162 + i * 22
        s += line(x + 20, y, x + 130, y, 3, DARKSTEEL) + line(x + 145, y, x + 180, y, 3, DARKSTEEL)
        s += path(f'M{x+186},{y-6} l5,6 l9,-11', sw=3, extra=f'stroke="{LEAF}"')
    return s
b += receipt(90, 'Plain run', SKY) + receipt(510, 'Split run', SUN)
b += text(400, 210, '=', 70)
# magnifier
b += circ(400, 290, 46, '#DFF6FF') + line(432, 322, 470, 360, 10)
b += text(400, 284, 'every', 15) + text(400, 304, 'row', 15)
# stamp
b += g(rect(300, 96, 200, 62, 'none', 12, 5, f'stroke="{CORAL}"') + text(400, 125, 'IDENTICAL', 24, fill=CORAL)
       + text(400, 148, '284,416 / 284,416', 15, fill=CORAL), 'transform="rotate(-6 400 127)"')
files['05-checked-to-the-last-row'] = svg("Checked to the Last Row", '#EAF8F2', b,
    "Split or not, the answer is the same: every line compared, every line identical.")

# 6. Everyone on the Same Clock --------------------------------------------------------------------------
b = ''
def clock(cx, cy, r, fill, hand=0):
    s = circ(cx, cy, r, fill) + circ(cx, cy, r * 0.78, PAPER, 3)
    for i in range(12):
        a = math.pi / 6 * i
        s += line(cx + r * 0.62 * math.cos(a), cy + r * 0.62 * math.sin(a), cx + r * 0.72 * math.cos(a), cy + r * 0.72 * math.sin(a), 2.5)
    s += line(cx, cy, cx + r * 0.5 * math.cos(hand), cy + r * 0.5 * math.sin(hand), 4) + line(cx, cy, cx, cy - r * 0.35, 4)
    return s
# tower
b += g(rect(345, 92, 110, 190, STEEL, 12), 'filter="url(#soft)"') + poly([(335, 96), (400, 70), (465, 96)], CORAL) + clock(400, 160, 42, SUN, -0.3)
b += text(400, 240, 'master', 15) + text(400, 258, 'clock', 15)
# workers
for i, x in enumerate([90, 230, 570, 710]):
    y = 330
    b += rect(x - 50, y - 8, 100, 50, '#E2C59A', 8, 3)   # desk
    b += circ(x, y - 40, 22, PINK, 3) + eyes(x, y - 44, 14, 5)   # head
    b += clock(x, y - 92, 22, TEAL, -0.3)
    b += text(x, y + 66, f'piece {i+1}', 14, weight='400')
    # tick bubbles
    sx, sy = 400, 200; ex, ey = x, y - 118
    b += line(sx + (ex - sx) * 0.2, sy + (ey - sy) * 0.2, sx + (ex - sx) * 0.92, sy + (ey - sy) * 0.92, 2.5, DARKSTEEL, '4 8')
    for t in (0.35, 0.6, 0.85):
        b += circ(sx + (ex - sx) * t, sy + (ey - sy) * t, 6, SUN, 2.5)
b += text(400, 300, 'tick... tick...', 16, weight='400')
files['06-everyone-on-the-same-clock'] = svg("Everyone on the Same Clock", '#FFF1F5', b,
    "Clock ticks keep every piece in step, so timers fire at exactly the same moment.")

# 7. Heaviest First --------------------------------------------------------------------------------------
b = ''
def truck(x, y, loads, col):
    s = rect(x, y, 230, 80, PAPER, 8, 4) + rect(x + 230, y + 22, 60, 58, col, 10, 4) + rect(x + 244, y + 32, 30, 20, '#DFF6FF', 4, 3)
    s += circ(x + 40, y + 86, 14, INK, 2) + circ(x + 190, y + 86, 14, INK, 2) + circ(x + 262, y + 86, 14, INK, 2)
    xx = x + 8
    for w_, c in loads:
        s += rect(xx, y + 80 - 62, w_, 56, c, 6, 3); xx += w_ + 4
    return s
b += text(40, 100, 'Biggest crates loaded first, smaller ones fill the gaps', 18, 'start', '400')
b += truck(60, 120, [(110, CORAL), (50, SKY), (50, LEAF)], TEAL)
b += truck(430, 120, [(96, GRAPE), (64, SUN), (50, PINK)], SUN)
b += truck(60, 260, [(88, SUN), (76, SKY), (46, CORAL)], CORAL)
b += truck(430, 260, [(80, LEAF), (70, GRAPE), (60, SKY)], GRAPE)
b += line(358, 112, 358, 380, 4, LEAF, '10 8') + line(728, 112, 728, 380, 4, LEAF, '10 8')
b += text(720, 106, 'all done together', 15, 'end', '400')
files['07-heaviest-first'] = svg("Heaviest First", '#EEF3FF', b,
    "The biggest jobs start first, so every core finishes at about the same time.")

# 8. The Sieve -------------------------------------------------------------------------------------------
b = ''
# mixed pile falling in
for i, k in enumerate('csxtxhcxs'):
    b += piece(k, 300 + (i % 5) * 50, 105 + (i // 5) * 32, 12)
# sieve
b += path('M250,175 L550,175 L520,235 L280,235 Z', STEEL)
for i in range(9):
    b += line(290 + i * 28, 182, 296 + i * 26, 228, 2.5, DARKSTEEL)
b += text(400, 212, 'SIEVE', 18)
# left: noise to "store as is"
b += arrow(280, 235, 200, 300, INK)
b += rect(70, 300, 170, 100, '#E6E9EF', 12) + text(155, 330, 'store as is', 16)
for i in range(4):
    b += piece('x', 105 + i * 34, 370, 11)
# right: compressible to compactor
b += arrow(520, 235, 590, 290, INK)
b += g(rect(540, 290, 200, 110, TEAL, 16), 'filter="url(#soft)"') + text(640, 322, 'compactor', 17)
b += rect(600, 336, 80, 46, PAPER, 8, 3)
for i, k in enumerate('cst'):
    b += piece(k, 618 + i * 22, 359, 7)
b += text(155, 286, "can't shrink", 15, weight='400') + text(640, 280, 'will shrink', 15, weight='400')
files['08-the-sieve'] = svg("The Sieve", '#F6F1E9', b,
    "Data that cannot shrink is set aside, so no effort is wasted on it.")

# 9. Squeeze and Restore ---------------------------------------------------------------------------------
b = ''
def pile(cx, cy):
    s = ''
    for i, k in enumerate('cstchsctsh'):
        s += piece(k, cx - 60 + (i % 5) * 30, cy - (i // 5) * 30, 13)
    return s
b += pile(130, 280) + text(130, 330, 'original', 16, weight='400')
# press
b += rect(250, 120, 150, 30, DARKSTEEL, 6) + line(325, 150, 325, 196, 8) + rect(270, 196, 110, 24, STEEL, 6)
b += rect(262, 286, 126, 12, DARKSTEEL, 4) + rect(290, 232, 70, 54, SUN, 8)
b += text(325, 266, 'small', 15)
b += text(325, 110, 'squeeze', 18)
b += arrow(205, 270, 255, 270) + arrow(395, 260, 455, 260)
# spring unpacker
b += rect(470, 180, 120, 106, CORAL, 14) + path('M500,180 l12,-14 l12,14 l12,-14 l12,14 l12,-14 l12,14', sw=4)
b += text(530, 240, 'restore', 17)
b += arrow(600, 250, 640, 250)
b += pile(710, 280) + text(710, 330, 'identical', 16, weight='400')
b += text(400, 380, 'byte-for-byte the same', 18)
files['09-squeeze-and-restore'] = svg("Squeeze and Restore", '#EAF6FF', b,
    "Lossless: what comes back out is exactly what went in.")

# 10. The Race -------------------------------------------------------------------------------------------
b = ''
results = [('Tesseract OCR', 6.6, CORAL), ('jq', 6.4, SKY), ('Apache Tika', 5.9, GRAPE), ('Ghostscript', 5.1, SUN), ("Wireshark's tshark", 2.9, LEAF)]
x0, x1 = 230, 700
b += text(40, 98, 'How many times faster, same answers (12-thread desktop)', 17, 'start', '400')
for i, (name, v, col) in enumerate(results):
    y = 120 + i * 56
    b += text(220, y + 30, name, 17, 'end')
    b += rect(x0, y + 8, x1 - x0, 34, '#E6EAF2', 17, 3)
    w = (x1 - x0) * v / 7.0
    b += rect(x0, y + 8, w, 34, col, 17, 3)
    b += text(x0 + w - 12, y + 31, f'{v}x', 16, 'end')
    # little runner flag
    b += line(x0 + w + 14, y + 4, x0 + w + 14, y + 44, 3) + poly([(x0 + w + 14, y + 4), (x0 + w + 38, y + 12), (x0 + w + 14, y + 20)], col, 3)
b += line(x0, 396, x0, 116, 3, INK)
b += text(x1, 408, '1x = the program on its own', 14, 'end', '400')
files['10-the-race'] = svg("The Race", '#FFF8E1', b,
    "Same programs, same output, finished sooner with Harrington's Sets.")

for name, s in files.items():
    open(os.path.join(OUT, name + '.svg'), 'w').write(s)
print(len(files), 'svg files written to', OUT)
