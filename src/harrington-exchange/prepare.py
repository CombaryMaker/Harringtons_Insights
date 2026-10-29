# Builds the biased-random test set. Sources: NIST SP800-90B_EntropyAssessment bin/ (github.com/usnistgov/SP800-90B_EntropyAssessment),
# MNE testing data BDF (github.com/mne-tools/mne-testing-data, BDF/multiple_annotation_chans.bdf). Synthetic files use seed 2360932695.
import numpy as np
N='nist/bin/'
def pack(src,bits,dst):
    a=np.frombuffer(open(src,'rb').read(),np.uint8)&((1<<bits)-1)
    b=np.unpackbits(a[:,None],axis=1)[:,8-bits:].ravel(); open(dst,'wb').write(np.packbits(b).tobytes())
pack(N+'truerand_1bit.bin',1,'truerand_1bit_packed.bin'); pack(N+'biased-random-bits.bin',1,'biased_bits_packed.bin')
pack(N+'truerand_4bit.bin',4,'truerand_4bit_packed.bin'); pack(N+'ringOsc-nist.bin',1,'ringOsc_packed.bin')
rng=np.random.default_rng(2360932695)
def packsym(v,bits,dst):
    b=((v[:,None]>>np.arange(bits-1,-1,-1))&1).astype(np.uint8).ravel(); open(dst,'wb').write(np.packbits(b).tobytes())
packsym(np.minimum(rng.geometric(0.25,838861)-1,31),5,'synthetic_5bit_skewed.bin')
packsym(np.clip(np.round(rng.normal(2048,40,349525)),0,4095).astype(np.int64),12,'synthetic_12bit_noise.bin')
