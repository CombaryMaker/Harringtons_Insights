# wgroup.py - group the lines of a Wikipedia slice by kind (Michael Harrington's segment idea, 10 Nov 2026). CC0 1.0 (public domain), see LICENSE.txt.
# Kinds: 0 prose, 1 heading, 2 list, 3 template/table/fact box, 4 indented (code or XML page data), 5 category or
# language link, 6 line starting with an escaped tag (&lt;...), 7 any line inside a References / External links /
# See also / Notes section, 8 blank. Writes FILE.wg (grouped, kinds in that order, each kind in original order) and
# FILE.wc (control: odd lines then even lines). Prints the cost of the note that undoes the grouping: the kind of every
# line, coded with an adaptive model on the previous two kinds.
import sys, math, re
f = sys.argv[1]; d = open(f, 'rb').read()
lines = d.split(b'\n'); lines = [l + b'\n' for l in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
assert b''.join(lines) == d
REF = re.compile(rb'eference|xternal link|ee also|Notes|urther reading|ibliograph|Sources')
kinds = []; refsec = False
for l in lines:
    s = l.rstrip(b'\n')
    if s.startswith(b'='): k = 1; refsec = bool(REF.search(s))
    elif b'<page>' in s or b'<title>' in s: k = 4; refsec = False
    elif not s.strip(): k = 8
    elif refsec: k = 7
    elif s[:1] in b'*#:;': k = 2
    elif s[:1] in b'{|}!': k = 3
    elif s[:1] == b' ': k = 4
    elif re.match(rb'\[\[[a-z]', s): k = 5
    elif s.startswith(b'&lt;'): k = 6
    else: k = 0
    kinds.append(k)
K = 9
out = b''.join(l for g in range(K) for l, k in zip(lines, kinds) if k == g)
open(f + '.wg', 'wb').write(out)
open(f + '.wc', 'wb').write(b''.join(lines[1::2]) + b''.join(lines[0::2]))
cnt = {}; bits = 0
for i, k in enumerate(kinds):
    c = cnt.setdefault((kinds[i-2] if i > 1 else -1, kinds[i-1] if i else -1), [0.4] * K); bits += -math.log2(c[k] / sum(c)); c[k] += 1
share = [sum(len(l) for l, k in zip(lines, kinds) if k == g) / len(d) * 100 for g in range(K)]
print(f"{f}: {len(lines)} lines; note cost {bits/8:.0f} bytes; share % " + ' '.join(f'{x:.1f}' for x in share))
