import sys, numpy as np
y = np.loadtxt(sys.argv[1]); 
for f in sys.argv[2:]:
    s = np.loadtxt(f); assert len(s) == len(y)
    r = s.argsort().argsort() + 1.0; # ties: average via scipy
    from scipy.stats import rankdata; r = rankdata(s); P = y.sum(); N = len(y) - P
    auc = (r[y == 1].sum() - P * (P + 1) / 2) / (P * N)
    k = int(P); top = np.argsort(-s)[:k]; prec = y[top].mean()
    print(f'{f.split("/")[-1]}: ROC AUC {auc:.4f}  precision@{k} {prec:.3f}')
