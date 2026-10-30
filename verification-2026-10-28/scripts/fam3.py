from cli import *
import subprocess,re,json,statistics as st,sys
sys.path.insert(0,S+'/qx'); from var2 import rec
H=W+'/h.out'
def hcm(f,mode,only):
    o=f'{W}/{os.path.basename(f)}.{mode}{only}.hcm'
    return m(f'hcmx {mode} ONLY={only}',f,f'rm -f {o}; {HX} {mode} {f} {o} >/dev/null',f'{HX} d {o} {H} >/dev/null',o,env={'HCMX_ONLY':str(only)},cmp_raw=H)
def isbin(f): b=open(f,'rb').read(); return sum(1 for x in b if x<9 or 13<x<32 or x==127)>len(b)/10
# HCM v3.3 fast wins vs rivals (rival sizes from the book; ZPAQ binary not available here)
for fn in ('cp.html','fields.c','lcet10.txt'):
    f=CT+fn; c=hcm(f,'c',32); ox=f'{W}/{fn}.xz'; b=m('xz -9e (live reference rival)',f,f'xz -9e -T1 -c {f} > {ox}',f'xz -dc {ox}',ox)
    log('HCM v3.3 fast',fn,os.path.getsize(f),b,c,dict(note='base = xz -9e re-run live; ZPAQ -m5 not re-run'))
# hcm-trit v2 vs v3.3 (fast)
for f in (CT+'cp.html',CT+'ptt5',SL+'xml'):
    bn=16 if isbin(f) else 0
    log('hcm-trit v2 vs v3.3 (fast)',os.path.basename(f),os.path.getsize(f),hcm(f,'c',32|bn),hcm(f,'c',96|bn))
# HCM-Bin kennedy max
f=CT+'kennedy.xls'; log('HCM-Bin v1 vs v3.3 (max)','kennedy.xls',os.path.getsize(f),hcm(f,'x',48),hcm(f,'x',240))
# FLAC
for sp,opt in (('george','-b 1152 -e -r 8'),('jackson','-b 1152 -e'),('theo','-b 576 -e -p')):
    w=S+f'/av/wav/{sp}.wav'
    def fl(tag,o_):
        o=f'{W}/{sp}.{tag}.flac'; raw=f'{W}/{sp}.{tag}.raw'
        r=m(f'flac {o_}',w,f'{FL} -s -f -o {o} {o_} {w}',f'{FL} -d -s -f -o {W}/{sp}.{tag}.dec.wav {o}',o,cmp_raw=f'{W}/{sp}.{tag}.dec.wav')
        return r
    log('FLAC block size',sp,os.path.getsize(w),fl('base','-8'),fl('hi','-8 '+opt),dict(note='round trip compared as whole WAV file (flac restores the identical WAV)'))
# WebP
CW=S+'/av/libwebp/b/cwebp'; DW=S+'/av/libwebp/b/dwebp'; K=S+'/av/data/kodak/PhotoCD_PCD0992/'
for im,opt in (('01','-m 5 -q 75'),('10','-m 4 -q 50'),('12','-m 4 -q 100')):
    p=K+im+'.png'; ref=f'{W}/k{im}.ref.ppm'; subprocess.run([DW,'-quiet','-ppm','-o',ref,p] if False else ['python3','-c',f"from PIL import Image;Image.open('{p}').convert('RGB').save('{ref}')"],check=True)
    def wp(tag,o_):
        o=f'{W}/k{im}.{tag}.webp'; d=f'{W}/k{im}.{tag}.ppm'
        return m(f'cwebp -lossless {o_}',ref,f'{CW} -quiet -lossless {o_} {p} -o {o}',f'{DW} -quiet -ppm {o} -o {d}',o,cmp_raw=d)
    log('WebP lossless search',im,os.path.getsize(p),wp('base','-z 9'),wp('hi',opt))
