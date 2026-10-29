# Extract raw telemetry streams: (1) IRIG-106 Ch10 PCM payload (packet headers stripped), (2) PX4 ULog per-topic channels, column-major.
import struct,sys,numpy as np
S_='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad/tel/'
d=open(S_+'src/pychapter10/tests/pcm.c10','rb').read(); p=0; pcm=bytearray(); types={}
while p+24<=len(d):
    sync,ch,plen,dlen,ver,seq,flags,dtype=struct.unpack_from('<HHIIBBBB',d,p)
    if sync!=0xEB25: p+=1; continue
    hl=24+(12 if flags&0x80 else 0); types[dtype]=types.get(dtype,0)+1
    if dtype==0x09: pcm+=d[p+hl+4:p+hl+dlen]
    p+=plen
print('ch10 packet types',types,'pcm payload bytes',len(pcm))
open(S_+'raw/ch10_pcm_payload.bin','wb').write(bytes(pcm))
sys.path.insert(0,S_+'src/pyulog')
from pyulog import ULog
u=ULog(S_+'src/pyulog/test/sample.ulg')
for want in ('sensor_combined','vehicle_attitude','vehicle_local_position','battery_status','sensor_baro','vehicle_gps_position','actuator_outputs'):
    ds=[x for x in u.data_list if x.name==want]
    if not ds: continue
    x=ds[0]; cols=[np.asarray(v) for k,v in x.data.items() if k!='timestamp' and not k.startswith('_padding')]
    b=b''.join(c.tobytes() for c in cols); open(S_+f'raw/px4_{want}.bin','wb').write(b); print(want,len(x.data['timestamp']),'rows',len(cols),'fields',len(b),'bytes', sorted({str(c.dtype) for c in cols}))
