# runs a command, prints wall time and peak memory (max RSS) of the process tree's largest member
import os, sys, time, subprocess
t=time.time(); p=subprocess.Popen(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
_,st,ru=os.wait4(p.pid,0); err=p.stderr.read().decode(errors='replace').strip().splitlines()
print("rc=%d wall=%.1fs peakRSS=%.1fMiB | %s"%(os.waitstatus_to_exitcode(st), time.time()-t, ru.ru_maxrss/1024, err[-1] if err else ''))
