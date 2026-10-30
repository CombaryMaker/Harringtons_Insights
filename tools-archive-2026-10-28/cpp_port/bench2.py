import subprocess, os, time, json, shutil, sys, filecmp
SP=os.path.abspath('..'); SRC=SP+'/cant_dl/canterbury-corpus/canterbury/'
B=os.path.abspath('bench_tmp'); shutil.rmtree(B,ignore_errors=True); os.makedirs(B)
FILES=['alice29.txt','asyoulik.txt','cp.html','fields.c','grammar.lsp','kennedy.xls','lcet10.txt','plrabn12.txt','ptt5','sum','xargs.1']
def run(cmd, stdout=None, cwd=None):
    t=time.perf_counter()
    p=subprocess.Popen(cmd, stdout=stdout or subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd)
    _,status,ru=os.wait4(p.pid,0)
    wall=time.perf_counter()-t
    assert os.waitstatus_to_exitcode(status)==0, cmd
    return wall, ru.ru_utime+ru.ru_stime, ru.ru_maxrss
def tool(name, f):
    src=SRC+f; d=B+'/'+name; os.makedirs(d,exist_ok=True)
    out=f'{d}/{f}.cmp'; back=f'{d}/{f}.back'
    if name=='HCM C++':
        c=run([os.path.abspath('hcm_bench'),'c',src,out]); x=run([os.path.abspath('hcm_clang'),'d',out,back])
    elif name=='ZPAQ -m5':
        w=f'{d}/w_{f}'; os.makedirs(w,exist_ok=True); shutil.copy(src,w+'/'+f); arc=f'{d}/{f}.zpaq'
        if os.path.exists(arc): os.remove(arc)
        c=run([SP+'/zpaqbin','a',arc,f,'-m5','-t1'],cwd=w); out=arc
        xd=f'{d}/x_{f}'; shutil.rmtree(xd,ignore_errors=True); os.makedirs(xd)
        x=run([SP+'/zpaqbin','x',arc,'-to',xd,'-t1','-force'],cwd=w); back=xd+'/'+f
    elif name in ('xz -9e','bzip2 -9'):
        prog=['xz','-9e'] if name.startswith('xz') else ['bzip2','-9']
        with open(out,'wb') as o: c=run(prog+['-k','-c',src],stdout=o)
        with open(back,'wb') as o: x=run([prog[0],'-d','-c',out],stdout=o)
    elif name=='cmix-lite v2':
        exe=os.path.abspath('../cmp_src/cmix-lite/cmix_v2')
        c=run([exe,'c',src,out]); x=run([exe,'d',out,back])
    ok=filecmp.cmp(src,back,shallow=False)
    return dict(tool=name,file=f,size=os.path.getsize(src),out=os.path.getsize(out),cw=c[0],cc=c[1],cm=c[2],dw=x[0],dc=x[1],dm=x[2],ok=ok)
for name in ['HCM C++','ZPAQ -m5','xz -9e','bzip2 -9']:
    for f in FILES:
        print(json.dumps(tool(name,f)),flush=True)
