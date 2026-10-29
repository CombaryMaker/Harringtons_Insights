import sys,json,numpy as np,subprocess,lzma; sys.path.insert(0,'.'); from he3 import *
S_='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'
D={'12bit_noise':(open('synthetic_12bit_noise.bin','rb').read(),12,4),
   'biased_bits':(open('biased_bits_packed.bin','rb').read(),21,13),
   '5bit_skewed':(open('synthetic_5bit_skewed.bin','rb').read(),5,1),
   'biased_bytes':(open('nist/bin/biased-random-bytes.bin','rb').read(),16,8),
   'x-ray_512KB':(open(S_+'/sil/f/x-ray','rb').read()[:524288],16,8),
   'mr_512KB':(open(S_+'/sil/f/mr','rb').read()[:524288],16,8),
   'eeg_bdf_1MB':(open('mtd/BDF/multiple_annotation_chans.bdf','rb').read()[:1048576],24,16)}
def zs(b): return len(zc(b,19))
def shuffle(d,w):
    a=np.frombuffer(d[:len(d)//w*w],np.uint8).reshape(-1,w); return sum(zs(a[:,k].tobytes()) for k in range(w))+len(d)%w
def unpack_bytes(d,W):
    v,_=to_vals(d,W); nb=(W+7)//8; a=np.zeros((len(v),nb),np.uint8)
    for k in range(nb): a[:,nb-1-k]=((v>>np.uint64(8*k))&np.uint64(255)).astype(np.uint8)
    return zs(a.tobytes()), (sum(zs(a[:,k].tobytes()) for k in range(nb)) if nb>1 else None)
out={}
for name,(d,J,S) in D.items():
    r={'bytes':len(d),'J':J,'S':S,'zstd19':zs(d),'xz9e':len(lzma.compress(d,preset=9|lzma.PRESET_EXTREME))}
    if J in (16,24): r['byte_shuffle']=shuffle(d,J//8)
    if J%8: u,us=unpack_bytes(d,J); r['unpack_to_bytes']=u; r['unpack_plus_shuffle']=us
    for m in ('full','split','rename','norev'):
        best=None
        for lay in (0,1):
            e=encode(d,J,S,mode=m,lay=lay,level=19); ok=decode(e)==d
            if best is None or len(e)<best[0]: best=(len(e),lay,ok)
        r[m]={'size':best[0],'layout':['tight','byte-aligned'][best[1]],'roundtrip_ok':best[2]}
    out[name]=r
    print(name,json.dumps(r),flush=True)
json.dump(out,open('he3_ablation.json','w'),indent=1)
