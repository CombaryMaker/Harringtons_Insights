# hs_pcapsplit.py - Harrington Sets for one capture: every packet goes to a conversation set by host pair (both IP
# addresses, either direction; non-IP traffic is one set); sets are spread over N shards by bytes, largest first
# (Graham LPT), and each shard keeps original packet order. Michael Hugh Harrington, 2026. MIT License.
# Usage: hs_pcapsplit.py IN.pcap N OUTDIR      (classic pcap, Ethernet)
import struct, sys, os, heapq
src, k, od = sys.argv[1], int(sys.argv[2]), sys.argv[3]; os.makedirs(od, exist_ok=True)
d = open(src, 'rb').read(); gh = d[:24]; p = 24; pk = []; size = {}
while p + 16 <= len(d):
    cl = struct.unpack_from('<I', d, p + 8)[0]; b = d[p + 16:p + 16 + cl]; et = b[12:14] if len(b) >= 14 else b''; o = 14
    while et in (b'\x81\x00', b'\x88\xa8') and len(b) >= o + 4: et = b[o + 2:o + 4]; o += 4
    if et == b'\x08\x00' and len(b) >= o + 20: a, c = b[o + 12:o + 16], b[o + 16:o + 20]
    elif et == b'\x86\xdd' and len(b) >= o + 40: a, c = b[o + 8:o + 24], b[o + 24:o + 40]
    else: a = c = b''
    key = min(a, c) + max(a, c); pk.append((p, 16 + cl, key)); size[key] = size.get(key, 0) + 16 + cl; p += 16 + cl
heap = [(0, i) for i in range(k)]; shard = {}
for key, s in sorted(size.items(), key=lambda x: -x[1]):
    load, i = heapq.heappop(heap); shard[key] = i; heapq.heappush(heap, (load + s, i))
fs = [open(f'{od}/shard{i:02d}.pcap', 'wb') for i in range(k)]
for f in fs: f.write(gh)
for p, l, key in pk: fs[shard[key]].write(d[p:p + l])
for f in fs: f.close()
print(f'{len(pk)} packets, {len(size)} host-pair sets, {k} shards')
