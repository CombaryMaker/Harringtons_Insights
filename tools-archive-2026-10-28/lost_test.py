import sys, json, math
import numpy as np
S='/tmp/claude-0/-home-claude/8920de7d-b851-5d17-a34c-ae2e2bff24d6/scratchpad'; sys.path.insert(0,S)
import pipeline as PL, hcm_context_balance as H
C=S+'/cant_dl/canterbury-corpus/canterbury/'
VARIANTS={
 'Sort8 top8':lambda b:PL.S_apply(b,8,8), 'Sort8 top16':lambda b:PL.S_apply(b,8,16),
 'Sort8 top32':lambda b:PL.S_apply(b,8,32), 'Sort8 top64':lambda b:PL.S_apply(b,8,64),
 'Sort8 full':lambda b:PL.S_apply(b,8,256), 'Sort4 full':lambda b:PL.S_apply(b,4,16),
 'HaBiTS pass':lambda b:PL.H_apply(b), 'Juggle best trigger':lambda b:PL.J_apply(b,1,PL.K0),
 'TSort5 full':lambda b:PL.TS_apply(b,5,243), 'TSort3 full':lambda b:PL.TS_apply(b,3,27)}
f=sys.argv[1]; names=sys.argv[2:] or list(VARIANTS)
data=open(C+f,'rb').read(); bits=np.unpackbits(np.frombuffer(data,np.uint8))
for nm in names:
    y,hdr,rec=VARIANTS[nm](bits)
    blob=H.compress(np.packbits(y).tobytes())
    extra=math.ceil((hdr+3+32)/8)      # transform table/trigger + pad count + bit length
    print(json.dumps([f,nm,len(blob)+extra]),flush=True)
