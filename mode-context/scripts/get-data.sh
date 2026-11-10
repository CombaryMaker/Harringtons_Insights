#!/bin/bash
# Downloads the public test texts into ./data and strips the Project Gutenberg header and footer.
# enwik8 (Hutter Prize file, first 100 MB of English Wikipedia): http://mattmahoney.net/dc/enwik8.zip
set -e; mkdir -p data; cd data
for id in 1342 84 1661 76 345 11 64317 1260 174 2009 1232; do
  curl -sSL -A "Mozilla/5.0" -o pg$id.txt https://www.gutenberg.org/cache/epub/$id/pg$id.txt
  python3 - pg$id <<'PY'
import sys
f = sys.argv[1]; t = open(f + '.txt', 'rb').read()
s = t.find(b'*** START OF'); s = t.find(b'\n', s) + 1; e = t.find(b'*** END OF')
open(f + '.body', 'wb').write(t[s:e])
PY
done
[ -f enwik8 ] || { curl -sSL -o enwik8.zip http://mattmahoney.net/dc/enwik8.zip && unzip -q enwik8.zip; }
for o in 0 40 80; do dd if=enwik8 of=s$o bs=1M skip=$o count=4 status=none; done   # three 4 MiB slices
ls -l
