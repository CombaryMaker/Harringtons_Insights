from cli import *
import json
mod={json.loads(l)['file']:json.loads(l) for l in open(S+'/modern/results.jsonl')}
# ---- xz-hi: hdfs.log, openstack.log (modern cfg), xz-0 mr (detected delta 2)
for fn in ('hdfs.log','openstack.log'):
    f=MD+fn; cfg=mod[fn]['res']['xz-hi (vs xz -9e)']['cfg']
    ob,eb=xzs(f,'-9e'); b=m('xz -9e',f,eb,f'xz -dc -T1 {ob}',ob)
    oc,ec=xzs(f,'--format=xz '+cfg,'.hi.xz'); c=m('xz-hi '+cfg,f,ec,f'xz -dc -T1 {oc}',oc)
    log('xz-hi',fn,os.path.getsize(f),b,c)
f=SL+'mr'
ob,eb=xzs(f,'-0'); b=m('xz -0',f,eb,f'xz -dc -T1 {ob}',ob)
oc,ec=xzs(f,'--delta=dist=2 --lzma2=preset=0','.d.xz'); c=m('xz -0 + delta 2',f,ec,f'xz -dc -T1 {oc}',oc)
log('xz-hi','xz-0 mr',os.path.getsize(f),b,c)
# ---- 7z-hi
SET={'asyoulik.txt':'-m0=LZMA2:d=64m:fb=273:lc=3:lp=0:pb=0','fields.c':'-m0=LZMA2:d=64m:fb=64:lc=1:lp=0:pb=0','kennedy.xls':'-m0=LZMA2:d=64m:fb=273:lc=1:lp=0:pb=0 -mf=Delta:13'}
for fn,s in SET.items():
    f=CT+fn
    def z7(tag,opt):
        o=f'{W}/{fn}.{tag}.7z'
        return m('7z '+opt,f,f'rm -f {o}; {Z7} a -t7z -mmt1 {opt} {o} {f} >/dev/null',f'{Z7} e -so {o} 2>/dev/null',o)
    log('7z-hi',fn,os.path.getsize(f),z7('base','-mx9'),z7('hi','-mx9 '+s))
# ---- RAR sao (only win)
f=SL+'sao'
def rr(tag,opt):
    o=f'{W}/sao.{tag}.rar'
    return m('rar '+opt,f,f'rm -f {o}; {RAR} a -m5 -ma5 -mt1 -ep -inul {opt} {o} {f}',f'{UNRAR} p -inul {o}',o)
log('RAR','sao',os.path.getsize(f),rr('base',''),rr('mcd','-mcd-'))
# ---- bzip2-hi
for fn in ('plrabn12.txt','sum','xargs.1'):
    f=CT+fn; ob=f'{W}/{fn}.base.bz2'
    b=m('bzip2 -9',f,f'bzip2 -9c {f} > {ob}',f'bzip2 -dc {ob}',ob)
    t=f'{W}/bzh/{fn}'; os.makedirs(f'{W}/bzh',exist_ok=True); shutil.copy(f,t)
    c=m('bzip2-hi',f,f'rm -f {t}.bz2; {BZH} {t} >/dev/null 2>&1',f'bzip2 -dc {t}.bz2',t+'.bz2')
    log('bzip2-hi',fn,os.path.getsize(f),b,c)
