# grid.py - how much do packJPG's own segment/noise settings, chosen per file, save over its automatic choice?
import subprocess, os, sys, shutil, itertools, concurrent.futures as cf, glob
J = os.path.dirname(os.path.abspath(__file__)); D = J + '/../pjg_y/packJPG_dev'; R = J + '/../pjg_y/packJPG_rel'
imgs = sorted(glob.glob(J + '/img/*.jpg'))[:int(sys.argv[1])]
S = [1, 2, 3, 4, 6, 8, 10, 12, 15, 20, 25, 30, 40, 49]; T = range(0, 11)
def size(img, args, tag):
    w = f'{J}/w/{os.path.basename(img)}_{tag}'; os.makedirs(w, exist_ok=True); shutil.copy(img, w + '/a.jpg')
    subprocess.run([D if args else R, 'a'] + args + ['-np', '-o', w + '/a.jpg'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return os.path.getsize(w + '/a.pjg')
def one(img):
    auto = size(img, [], 'auto'); best = (auto, 'auto')
    for s, t in itertools.product(S, T):
        z = size(img, ['-dev', f'-s{s}', f'-t{t}'], f's{s}t{t}')
        if z < best[0]: best = (z, f's{s} t{t}')
    return os.path.basename(img), os.path.getsize(img), auto, best
with cf.ProcessPoolExecutor(2) as ex:
    tj = ta = tb = 0
    for name, j, a, (b, cfg) in ex.map(one, imgs):
        tj += j; ta += a; tb += b
        print(f'{name} jpg {j:>9,} auto {a:>9,} best {b:>9,} ({cfg})  saved {a-b:>6,} bytes', flush=True)
print(f'TOTAL jpg {tj:,} auto {ta:,} best {tb:,}  saved {ta-tb:,} bytes ({100*(ta-tb)/ta:.2f}% of auto)')
