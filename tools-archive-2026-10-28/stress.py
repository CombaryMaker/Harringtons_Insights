import sys, random
sys.path.insert(0,'.')
import hcm_context_balance as H
H.TBITS=12; H.MASK=(1<<12)-1          # small tables so each round trip starts fast; same coder and match logic
random.seed(11)
cases=[b'', b'\x00', b'\xff', b'a', b'\x00'*1000, b'\xff'*1000, b'ab'*700, bytes(range(256))*4,
       b'abcdefg'*300 + b'X', b'the cat sat on the mat. '*80]
for _ in range(110):
    n=random.choice([1,2,3,5,7,8,9,15,16,17,31,64,100,255,256,257,500,1000,2000,4000])
    kind=random.random()
    if kind<0.3: d=bytes(random.getrandbits(8) for _ in range(n))
    elif kind<0.6: d=bytes(random.choice(b'01') for _ in range(n))
    elif kind<0.8: seg=bytes(random.getrandbits(8) for _ in range(random.randint(1,12))); d=(seg*(n//len(seg)+1))[:n]
    else: d=bytes(random.choice([0,0,0,0,255,1]) for _ in range(n))
    cases.append(d)
bad=0
for d in cases:
    if H.decompress(H.compress(d))!=d: bad+=1
print(f"edge/stress round trips: {len(cases)-bad} of {len(cases)} exact (empty file, 1 byte, all-0, all-1, periodic, random, lengths 1-4000)")
d=b'the cat sat on the mat. '*80; blob=H.compress(d)
for cut in (1,4,16):
    out=H.decompress(blob[:-cut]); print(f"truncated by {cut} bytes: decoder reports error? no | output correct? {out==d}")
