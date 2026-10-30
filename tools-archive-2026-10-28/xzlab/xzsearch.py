# Compressor-side search for smaller standard .xz files (stock xz decodes them).
import lzma, os, sys, json, itertools
from multiprocessing import Pool
C='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/cant_dl/canterbury-corpus/canterbury'
FILES=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
BIN={'kennedy.xls','ptt5','sum'}
def comp(data, lc, lp, pb, pre=None, depth=512, nice=273, mf=lzma.MF_BT4, check=lzma.CHECK_CRC32, dict_size=None):
    d = dict_size or max(4096, len(data))
    f = [] if pre is None else [pre]
    f.append({"id": lzma.FILTER_LZMA2, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": d, "lc": lc, "lp": lp, "pb": pb, "nice_len": nice, "depth": depth, "mf": mf})
    return lzma.compress(data, format=lzma.FORMAT_XZ, check=check, filters=f)
def desc(c): return json.dumps(c, default=str)
def search(fn):
    data = open(os.path.join(C, fn), 'rb').read()
    base = lzma.compress(data, format=lzma.FORMAT_XZ, preset=9 | lzma.PRESET_EXTREME)   # = xz -9e
    cands = []
    for lc, lp, pb in itertools.product(range(5), range(3), range(3)):
        if lc + lp > 4: continue
        cands.append((len(comp(data, lc, lp, pb)), dict(lc=lc, lp=lp, pb=pb)))
    cands.sort(key=lambda x: x[0])
    best = cands[0][1]
    pres = [None]
    if fn in BIN:
        pres += [{"id": lzma.FILTER_DELTA, "dist": d} for d in (1, 2, 4, 8, 12, 13, 16, 26, 48, 216)]
        pres += [{"id": i} for i in (lzma.FILTER_X86, lzma.FILTER_SPARC, lzma.FILTER_POWERPC, lzma.FILTER_ARM, lzma.FILTER_ARMTHUMB, lzma.FILTER_IA64)]
    out = []
    for top in cands[:4]:
        p = top[1]
        for pre in pres:
            for depth in (512, 0, 4000):
                for nice in (273, 128):
                    try: n = len(comp(data, p['lc'], p['lp'], p['pb'], pre, depth, nice))
                    except Exception as e: continue
                    out.append((n, dict(p, pre=pre, depth=depth, nice=nice)))
    out.sort(key=lambda x: x[0])
    return fn, len(base), out[:6]
if __name__ == '__main__':
    with Pool(2) as pool:
        res = pool.map(search, FILES)
    json.dump(res, open('xzsearch.json', 'w'), default=str, indent=1)
    for fn, b, out in res: print(fn, 'xz -9e', b, 'best', out[0][0], out[0][1])
