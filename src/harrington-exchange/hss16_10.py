# Reference Harrington Sort Swap 16HE-10 table, built straight from the written spec.
def build():
    out, seen = [], set()
    def add(p):
        assert p not in seen; seen.add(p); out.append(p)
    for x in range(64): add('0'*10 + format(x,'06b'))           # zero-debt zone
    for t in range(1,7):                                          # tiers 1..6
        free = 6 - t
        for x in range(2**free):
            add('0'*(t-1) + '1' + '0'*10 + (format(x, f'0{free}b') if free else ''))
    rest = [format(v,'016b') for v in range(65536)]
    rest = [p for p in rest if p not in seen]
    rest.sort(key=lambda p: (p.count('1'), p))   # spec gives NO tie-break; here: numeric
    for p in rest: add(p)
    return out
t = build()
assert len(t) == 65536 and len(set(t)) == 65536 and t[-1] == '1'*16
open('hss16_10_reference.txt','w').write('\n'.join(t)+'\n')
print(t[:3], t[63:66], t[126:129])
# how many popcount ties the spec leaves unordered
from collections import Counter
print('largest unordered tie group:', max(Counter(p.count('1') for p in t[127:]).values()))
# first-10-bit cleanliness for the Juggle cut at bit 10
print('tier patterns with a 1 inside the juggle head:', sum('1' in p[:10] for p in t[64:127]))
