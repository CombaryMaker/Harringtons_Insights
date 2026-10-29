#!/usr/bin/env python3
"""lpaq1-h parallel blocks (HCM v3.5-style, Harrington's Insights).
Cuts the input into blocks, compresses each with lpaq1-h best-of (lpaq1h_best.py) in parallel,
and decodes the blocks in parallel. One process per block, up to the number of cores.
usage:  lpaq1h_par.py c LPAQ1H N BLOCK_MB INPUT OUTPUT      (compress)
        lpaq1h_par.py d LPAQ1H OUTFILE.lphp RESTORED         (decompress)
Format: b'LPHP' + 32-bit block size + 32-bit block count + 32-bit length per block + lpaq1/lpaq1-h streams."""
import os, sys, struct, subprocess, tempfile, shutil
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__))
def comp(exe, n, bmb, src, dst):
    B = int(bmb) << 20; data = open(src, 'rb').read(); tmp = tempfile.mkdtemp()
    parts = [data[i:i + B] for i in range(0, max(len(data), 1), B)]
    def one(k):
        p = os.path.join(tmp, f'b{k}'); open(p, 'wb').write(parts[k])
        subprocess.run([sys.executable, os.path.join(HERE, 'lpaq1h_best.py'), exe, n, p, p + '.z'], check=True, capture_output=True)
        return open(p + '.z', 'rb').read()
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex: zs = list(ex.map(one, range(len(parts))))
    with open(dst, 'wb') as f:
        f.write(b'LPHP' + struct.pack('>II', B, len(zs)) + b''.join(struct.pack('>I', len(z)) for z in zs) + b''.join(zs))
    shutil.rmtree(tmp)
def decomp(exe, src, dst):
    b = open(src, 'rb').read(); assert b[:4] == b'LPHP'
    B, nb = struct.unpack('>II', b[4:12]); lens = struct.unpack(f'>{nb}I', b[12:12 + 4 * nb]); p = 12 + 4 * nb
    tmp = tempfile.mkdtemp(); offs = []
    for L in lens: offs.append(p); p += L
    def one(k):
        z = os.path.join(tmp, f'b{k}.z'); open(z, 'wb').write(b[offs[k]:offs[k] + lens[k]])
        subprocess.run([exe, 'd', z, z + '.out'], check=True, capture_output=True)
        return open(z + '.out', 'rb').read()
    with ThreadPoolExecutor(os.cpu_count() or 2) as ex: outs = list(ex.map(one, range(nb)))
    open(dst, 'wb').write(b''.join(outs)); shutil.rmtree(tmp)
if __name__ == '__main__':
    if sys.argv[1] == 'c': comp(*sys.argv[2:7])
    else: decomp(*sys.argv[2:5])
