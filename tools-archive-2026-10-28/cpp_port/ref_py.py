import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + '/..')
import hcm_context_balance_v2 as H
SRC = os.path.dirname(os.path.abspath(__file__)) + '/../cant_dl/canterbury-corpus/canterbury/'
for f in sys.argv[1:]:
    d = open(SRC + f, 'rb').read()
    t = time.time(); b = H.compress(d); te = time.time() - t
    open(os.path.dirname(os.path.abspath(__file__)) + '/ref/' + f + '.hcm', 'wb').write(b)
    print(f, len(b), round(te, 1), flush=True)
