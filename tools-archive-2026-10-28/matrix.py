import sys, math, subprocess, os, time
import numpy as np
SCR='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,SCR)
import pipeline as PL, route_test as RT, juggle as JG
def byte_ac(data, order):   # adaptive byte coder, ideal arithmetic-coded length (counts + 1/2 each)
    cnt={}; tot={}; cost=0.0; h=b''
    for c in data:
        k=h[-order:] if order else b''
        d=cnt.get(k)
        if d is None: d=cnt[k]={}; tot[k]=0
        cost-=math.log2((d.get(c,0)+0.5)/(tot[k]+128.0))
        d[c]=d.get(c,0)+1; tot[k]+=1; h=(h+bytes([c]))[-3:]
    return cost
def run(label,data):
    n=len(data)*8; P=lambda b: f"{round(b/n*1e6):,}"
    bits=np.unpackbits(np.frombuffer(data,np.uint8))
    T=[('No transform',bits,0)]
    y,h,_=PL.S_apply(bits,8,256); T.append(('Sort8 full',y,h))
    y,h,_=PL.H_apply(bits); T.append(('HaBiTS pass (best)',y,h))
    y,h,_=PL.J_apply(bits,1,PL.K0); T.append(('Juggle (best trigger)',y,h))
    y,h,_=PL.TS_apply(bits,5,243); T.append(('TSort5 full',y,h))
    L,_=RT.bwt(data); T.append(('BWT',np.unpackbits(np.frombuffer(L,np.uint8)),32))
    T.append(('BWT > MTF',np.unpackbits(np.frombuffer(RT.mtf(L),np.uint8)),32))
    T.append(('Predictor ranks (3-byte)',np.unpackbits(np.frombuffer(RT.rank_transform(data,3),np.uint8)),0))
    print(f"== {label}: 1,000,000 bits down to (adaptive arithmetic coding, every header counted) ==")
    print(f"   {'Transform':28s} {'bit, no context':>16} {'bit, 16-bit ctx':>16} {'byte, 1-byte ctx':>17} {'byte, 2-byte ctx':>17}",flush=True)
    for name,b,h in T:
        by=np.packbits(b).tobytes()
        r=[PL.K0(b)+h+32, PL.KB(b)+h+32, byte_ac(by,1)+h+32, byte_ac(by,2)+h+32]
        print(f"   {name:28s} "+" ".join(f"{P(v):>16}" for v in r),flush=True)
text=open('/usr/lib/python3.12/pydoc_data/topics.py','rb').read()[:200000]
gate=subprocess.run(['node',SCR+'/gate_gen.js'],capture_output=True).stdout[:140000]
run(sys.argv[1], text if sys.argv[1]=='text' else gate)
