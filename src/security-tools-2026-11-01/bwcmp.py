# bwcmp.py - compare hs_binwalk merged hits with plain binwalk -l output (offset, signature name, description).
import json, sys
p = json.load(open(sys.argv[1]))[0]['Analysis']['file_map']; h = json.load(open(sys.argv[2]))
P = {(x['offset'], x['name'], x['description']) for x in p}; H = {(x['offset'], x['name'], x['description']) for x in h}
Po = {x['offset'] for x in p}; Ho = {x['offset'] for x in h}
print(f'plain {len(P)} hits, split {len(H)} hits, identical set: {P == H}; same offsets: {Po == Ho}; '
      f'only plain {len(P - H)}, only split {len(H - P)}')
for x in sorted(P - H)[:5]: print('  plain only', x[0], x[1], x[2][:90])
for x in sorted(H - P)[:5]: print('  split only', x[0], x[1], x[2][:90])
