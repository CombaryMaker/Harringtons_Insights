# Damaged-container tests for hs_wrap: each case must end with a clean error message, never a traceback or hang.
import subprocess, struct, sys, os
H, good = sys.argv[1], sys.argv[2]; d = bytearray(open(good, 'rb').read())
n, crc, nr, nb = struct.unpack_from('<QIII', d, 4)
cases = {
 'truncated half': d[:len(d) // 2],
 'header only': d[:24],
 'huge length': d[:4] + struct.pack('<Q', 1 << 60) + d[12:],
 'huge run count': d[:24] + d[24:25] + struct.pack('<I', 0xFFFFFFFF) + d[29:],
 'bad set label': d[:24] + bytes([7]) + d[25:],
 'block length +1': (lambda x: (struct.pack_into('<Q', x, 24 + 5 * nr + 1, struct.unpack_from('<Q', x, 24 + 5 * nr + 1)[0] + 1), x)[1])(bytearray(d)),
 'extra byte at end': d + b'\0',
 'flipped data byte': (lambda x: (x.__setitem__(len(x) - 100, x[len(x) - 100] ^ 0x55), x)[1])(bytearray(d)),
}
ok = 0
for name, blob in cases.items():
    open('bad.hsw', 'wb').write(blob)
    r = subprocess.run(['python3', H, 'd', 'bad.hsw', 'bad.out', '--decomp', 'xz -dc {in}'], capture_output=True, text=True, timeout=120)
    clean = r.returncode != 0 and 'Traceback' not in r.stderr
    ok += clean; print(f'{name:20s} exit {r.returncode}  {r.stderr.strip().splitlines()[-1][:90] if r.stderr.strip() else ""}')
print(f'{ok}/{len(cases)} clean errors')
