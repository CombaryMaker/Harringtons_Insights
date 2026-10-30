"""Streaming anomaly detector (river HalfSpaceTrees): reads JSON records on stdin, scores each before learning it,
writes one score per line. Marker lines ({"__hsw":N}) are copied through unchanged."""
import json, sys, os
sys.path.insert(0, os.environ.get('RIVER_SRC', '.'))
from river import anomaly, compose, preprocessing
m = compose.Pipeline(preprocessing.MinMaxScaler(), anomaly.HalfSpaceTrees(n_trees=10, height=8, window_size=250, seed=42))
out = sys.stdout
for line in sys.stdin:
    if line.startswith('{"__hsw":'): out.write(line); continue
    r = json.loads(line); x = {'f1': r['f1'], 'f2': r['f2'], 'f3': r['f3']}
    out.write(f'{m.score_one(x):.6f}\n'); m.learn_one(x)
