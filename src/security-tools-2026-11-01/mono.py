# mono.py - make the joined test capture time-ordered: each capture file (classic pcap, usec) is shifted so it starts
# 1 s after the previous one ends, and packets inside a file are put in time order (stable). MIT License.
import struct, sys, os
src, dst = sys.argv[1], sys.argv[2]; os.makedirs(dst, exist_ok=True); t_end = 1_200_000_000 * 10**6
for i, fn in enumerate(sorted(f for f in os.listdir(src) if f.endswith('.pcap'))):
    d = open(os.path.join(src, fn), 'rb').read(); p = 24; pk = []
    while p + 16 <= len(d):
        s, u, cl, ol = struct.unpack_from('<IIII', d, p); pk.append((s * 10**6 + u, d[p + 16:p + 16 + cl], ol)); p += 16 + cl
    if not pk: continue
    pk.sort(key=lambda x: x[0]); off = t_end + 10**6 - pk[0][0]
    with open(os.path.join(dst, f'{i:03d}_{fn}'), 'wb') as f:
        f.write(d[:24])
        for t, b, ol in pk:
            t += off; f.write(struct.pack('<IIII', t // 10**6, t % 10**6, len(b), ol) + b)
    t_end = pk[-1][0] + off
print('end time', t_end // 10**6, 'span s', t_end // 10**6 - 1_200_000_000)
