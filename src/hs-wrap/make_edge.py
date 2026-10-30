# Makes the 16 boundary test files used by edge_test.sh (sizes around 4 KB, 64 KB, 1 MB, 2-3 MB and 16 MB).
import os, random
R = random.Random(7); txt = open('/usr/lib/python3.13/argparse.py', 'rb').read() * 20   # any text file will do
def mk(n, kind):
    if kind == 'txt': return txt[:n]
    if kind == 'rnd': return os.urandom(n)
    if kind == 'zero': return bytes(n)
    out = b''; k = 0
    while len(out) < n: out += (txt if k % 2 == 0 else os.urandom(65536 * 3))[:65536 * R.randint(1, 12)]; k += 1
    return out[:n]
for n, k in [(0,'txt'),(1,'txt'),(65535,'txt'),(65536,'txt'),(65537,'txt'),(65536,'rnd'),(4095,'rnd'),(4096,'rnd'),
             (131072,'zero'),((1<<20)-1,'mix'),(1<<20,'mix'),((1<<20)+1,'mix'),((2<<20)+1,'mix'),(3<<20,'mix'),
             ((16<<20)+1,'mix'),(1<<20,'zero')]:
    open(f'e_{n}_{k}', 'wb').write(mk(n, k))
