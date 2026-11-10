# Michael Hugh Harrington, 10 Nov 2026. CC0 1.0 (public domain), see LICENSE.txt.
# Paragraph reorder test: dialogue paragraphs (start with a quote) grouped first, others after (both in original order).
# Undo needs the 1-bit type per paragraph; its cost is measured with an adaptive order-2 model.
import sys, math
f = sys.argv[1]; d = open(f, 'rb').read()
lines = d.split(b'\n'); paras = []; cur = []
for ln in lines:
    if ln.strip(b'\r') == b'' :
        cur.append(ln)
        if any(x.strip(b'\r') for x in cur[:-1]) or len(cur) > 1:
            pass
    else:
        if cur and cur[-1].strip(b'\r') == b'' and any(x.strip(b'\r') for x in cur):
            paras.append(b'\n'.join(cur) + b'\n'); cur = []
    cur.append(ln) if ln.strip(b'\r') != b'' or not cur or cur[-1].strip(b'\r') != b'' or True else None
# simpler, robust split: on blank lines, keeping separators with the paragraph before
paras = []; buf = []
for ln in d.split(b'\n'):
    buf.append(ln)
    if ln.strip(b'\r') == b'' and any(x.strip(b'\r') for x in buf):
        paras.append(b'\n'.join(buf) + b'\n'); buf = []
if buf: paras.append(b'\n'.join(buf))
joined = b''.join(paras)
if joined.endswith(b'\n') and not d.endswith(b'\n'): joined = joined[:-1]
assert joined == d, 'split does not rebuild the file'
isq = lambda p: p.lstrip(b'\r\n ').startswith((b'"', b'\xe2\x80\x9c'))
t = [1 if isq(p) else 0 for p in paras]
ro = b''.join(p for p, k in zip(paras, t) if k) + b''.join(p for p, k in zip(paras, t) if not k)
open(f + '.ro', 'wb').write(ro)
cnt = {}; bits = 0
for i, k in enumerate(t):
    c = cnt.setdefault((t[i-2] if i > 1 else 0, t[i-1] if i else 0), [0.5, 0.5]); bits += -math.log2(c[k] / sum(c)); c[k] += 1
print(f"{f}: {len(paras)} paragraphs, {sum(t)} dialogue; type cost {bits/8:.0f} bytes")
