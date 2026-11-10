# reorder2.py - paragraph grouping schemes for Michael Harrington's paragraph idea (10 Nov 2026). CC0 1.0 (public domain), see LICENSE.txt.
# Writes FILE.g2 / .g3 / .g4 / .gc and prints the cost (bytes) of the per-paragraph note needed to undo each one,
# measured with an adaptive order-2 model over the note symbols.
#   g2  dialogue paragraphs (start with a quote) first, then the rest
#   g3  dialogue, narration, short paragraphs (one line: headings, chapter titles) - 3 groups
#   g4  as g3, but dialogue split by turn: 1st, 3rd, 5th... paragraph of each dialogue run vs 2nd, 4th... (a free guess
#       at "speaker A / speaker B"; the turn parity follows from the stored types, so the note costs the same as g3)
#   gc  control: odd-numbered paragraphs first, then even (same note cost as g2 would be at worst) - does any reorder help?
import sys, math
f = sys.argv[1]; d = open(f, 'rb').read()
paras = []; buf = []
for ln in d.split(b'\n'):
    buf.append(ln)
    if ln.strip(b'\r') == b'' and any(x.strip(b'\r') for x in buf):
        paras.append(b'\n'.join(buf) + b'\n'); buf = []
if buf: paras.append(b'\n'.join(buf))
j = b''.join(paras)
if j.endswith(b'\n') and not d.endswith(b'\n'): j = j[:-1]
assert j == d
def kind(p):
    s = p.lstrip(b'\r\n ')
    if s.startswith((b'"', b'\xe2\x80\x9c', b'\xe2\x80\x98')): return 0
    if s.count(b'\n') <= 2 and len(s.strip()) < 80: return 2
    return 1
t = [kind(p) for p in paras]
def cost(sym, K):
    cnt = {}; bits = 0
    for i, k in enumerate(sym):
        c = cnt.setdefault((sym[i-2] if i > 1 else -1, sym[i-1] if i else -1), [0.5] * K); bits += -math.log2(c[k] / sum(c)); c[k] += 1
    return bits / 8
def write(ext, groups, order):
    out = b''.join(paras[i] for g in order for i in range(len(paras)) if groups[i] == g)
    open(f + '.' + ext, 'wb').write(out)
g2 = [0 if k == 0 else 1 for k in t]
g4 = []; run = 0
for k in t:
    if k == 0: g4.append(0 if run % 2 == 0 else 3); run += 1
    else: g4.append(k); run = 0
write('g2', g2, [0, 1]); write('g3', t, [0, 1, 2]); write('g4', g4, [0, 3, 1, 2]); write('gc', [i % 2 for i in range(len(paras))], [1, 0])
print(f"{f} paragraphs {len(paras)} dialogue {t.count(0)} narration {t.count(1)} short {t.count(2)}  note cost g2 {cost(g2, 2):.0f} g3 {cost(t, 3):.0f} g4 {cost(t, 3):.0f} gc 0")
