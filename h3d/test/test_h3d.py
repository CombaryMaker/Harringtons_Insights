# h3d test on public CT series: h3d (whole files, exact rebuild checked) against JPEG XL e9, JPEG-LS and xz on the
# same files. Image codecs get the pixels, the rest of each file is added with xz (fair to them). 13 Nov 2026.
import sys, os, glob, time, lzma, subprocess, tempfile, filecmp, numpy as np, pydicom, imagecodecs as ic
H3D = os.environ.get('H3D', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'h3d'))   # path to the built h3d program
def series(d):
    fs = [f for f in glob.glob(d + '/*') if os.path.isfile(f)]
    def key(f):
        ds = pydicom.dcmread(f, stop_before_pixels=True); return int(getattr(ds, 'InstanceNumber', 0) or 0), f
    return sorted(fs, key=key)
for d in sys.argv[1:]:
    fs = series(d); raw = sum(os.path.getsize(f) for f in fs)
    with tempfile.TemporaryDirectory(dir='/tmp/claude-0') as t:
        s = time.time(); subprocess.run([H3D, 'c', t + '/a.h3d'] + fs, check=True, capture_output=True); c = time.time() - s
        os.mkdir(t + '/o'); s = time.time(); subprocess.run([H3D, 'd', t + '/a.h3d', t + '/o'], check=True, capture_output=True); dt = time.time() - s
        same = all(filecmp.cmp(f, t + '/o/' + os.path.basename(f), shallow=False) for f in fs); hb = os.path.getsize(t + '/a.h3d')
    px = []; rest = b''
    for f in fs:
        b = open(f, 'rb').read(); ds = pydicom.dcmread(f)
        if 'PixelData' not in ds: rest += b; continue          # files without an image: all their bytes go to xz
        a = ds.pixel_array; a = (a.astype(np.int32) - a.min()).astype(np.uint16) if a.dtype == np.int16 else a   # signed -> offset (one number per slice)
        i = b.rfind(b'\xe0\x7f\x10\x00'); rest += b[:i]; px.append(a)
    rx = len(lzma.compress(rest, preset=9 | lzma.PRESET_EXTREME))
    def per(enc, dec):
        s = time.time(); bs = [enc(a) for a in px]; c = time.time() - s; s = time.time(); ok = all(np.array_equal(dec(b).reshape(a.shape), a) for a, b in zip(px, bs)); dd = time.time() - s
        return sum(map(len, bs)) + rx, c, dd, ok
    rows = [('h3d', hb, c, dt, same)]
    rows.append(('JPEG XL e9 + xz headers',) + per(lambda a: ic.jpegxl_encode(a, lossless=True, effort=9), ic.jpegxl_decode))
    rows.append(('JPEG-LS + xz headers',) + per(lambda a: ic.jpegls_encode(a, level=0), ic.jpegls_decode))
    whole = b''.join(open(f, 'rb').read() for f in fs); s = time.time(); x = lzma.compress(whole, preset=9 | lzma.PRESET_EXTREME); c = time.time() - s
    s = time.time(); ok = lzma.decompress(x) == whole; rows.append(('xz -9e (all files)', len(x), c, time.time() - s, ok))
    print(f'== {d}  {len(fs)} files, {raw:,} bytes, {px[0].shape} {px[0].dtype}')
    for n, b, c, dd, ok in rows: print('   %-26s %12s bytes  %6.3fx  compress %6.1f s  decode %6.1f s  exact %s' % (n, f'{b:,}', raw / b, c, dd, ok), flush=True)
