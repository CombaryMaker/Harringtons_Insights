# x-ray zstd -19: stock vs record delta 2 (prefix-sum inverse, FASTU=1) + zstd -19 minMatch=5; 7 paired runs (+1 snapshot)
import subprocess,json,os,statistics as st
X='../sil/f/x-ray'; A=[(24.7,20.9)]
for i in range(7):
    s=json.loads(subprocess.run(['./rvb','zstd','19',X,'2','5'],capture_output=True,text=True,env=dict(os.environ,FASTU='1')).stdout)
    c=json.loads(subprocess.run(['./rvb','zstd','19',X,'2','5'],capture_output=True,text=True,env=dict(os.environ,FASTU='1',ZMM='5')).stdout)
    A.append((s['dec_plain_us']/1e3,c['dec_delta_us']/1e3))
a=[x for x,_ in A]; b=[y for _,y in A]
print(st.median(a),st.median(b),sum(y<x for x,y in A))
