# ng2pcap.py - pcapng to classic pcap (Ethernet interfaces only, microsecond timestamps). MIT License.
import struct, sys, os
def conv(src, dst):
    d = open(src, 'rb').read(); p = 0; e = '<'; ifs = []; out = []
    while p + 12 <= len(d):
        bt = struct.unpack(e + 'I', d[p:p+4])[0]
        if bt == 0x0A0D0D0A:
            e = '<' if d[p+8:p+12] == b'\x4d\x3c\x2b\x1a' else '>'; ifs = []
        bl = struct.unpack(e + 'I', d[p+4:p+8])[0]
        if bl < 12 or p + bl > len(d): break
        b = d[p+8:p+bl-4]
        if bt == 1:
            lt = struct.unpack(e + 'H', b[0:2])[0]; res = 6; q = 8
            while q + 4 <= len(b):
                oc, ol = struct.unpack(e + 'HH', b[q:q+4])
                if oc == 0: break
                if oc == 9: v = b[q+4]; res = (-(v & 0x7f) if v & 0x80 else v)
                q += 4 + ((ol + 3) & ~3)
            ifs.append((lt, res))
        elif bt == 6:
            iid, th, tl, cl, ol = struct.unpack(e + 'IIIII', b[:20])
            lt, res = ifs[iid] if iid < len(ifs) else (1, 6)
            if lt == 1:
                t = (th << 32) | tl
                us = t * 10**6 // (10**res) if res >= 0 else t * 10**6 >> (-res)
                out.append(struct.pack('<IIII', us // 10**6, us % 10**6, cl, ol) + b[20:20+cl])
        elif bt == 3 and ifs and ifs[0][0] == 1:
            ol = struct.unpack(e + 'I', b[:4])[0]; cl = min(ol, len(b) - 4)
            out.append(struct.pack('<IIII', 0, 0, cl, ol) + b[4:4+cl])
        p += bl
    if out:
        with open(dst, 'wb') as f:
            f.write(struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 262144, 1)); f.writelines(out)
    return len(out)
src, dstdir = sys.argv[1], sys.argv[2]; os.makedirs(dstdir, exist_ok=True); tot = 0; nf = 0
for fn in sorted(os.listdir(src)):
    if fn.lower().endswith('.pcapng'):
        n = conv(os.path.join(src, fn), os.path.join(dstdir, fn[:-2])); tot += n; nf += n > 0
print(f'{nf} files converted, {tot} Ethernet packets')
