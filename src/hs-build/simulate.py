# Uses the per-file compile times measured during the real build (hs_cc log: start, duration, object) to work out
# the compile-phase wall time on N cores for (a) make's own order (files started in the order the real make started
# them) and (b) heaviest-first (hs_build with a measured profile), both as greedy list scheduling: the next job in the
# list starts whenever a core is free. Compiles are independent of each other apart from a few generated sources,
# so this is a fair model of the compile phase (not of linking). Michael Hugh Harrington, MIT License.
import sys
rows = []
for l in open(sys.argv[1]):
    p = l.split('\t')
    if len(p) == 3: rows.append((float(p[0]), float(p[1]), p[2].strip()))
rows.sort()                                   # make's real start order
dur_make = [d for _, d, _ in rows]
dur_lpt = sorted(dur_make, reverse=True)
def makespan(durs, n):
    cores = [0.0] * n
    for d in durs:
        i = min(range(n), key=lambda k: cores[k]); cores[i] += d
    return max(cores), min(cores)
tot = sum(dur_make); mx = max(dur_make)
print(f'{len(rows)} compile jobs, total CPU {tot:.0f} s, longest single file {mx:.1f} s, mean {tot/len(rows):.2f} s')
print(f'{"cores":>5} {"ideal":>8} {"make order":>11} {"heaviest first":>15} {"gain":>7}   (idle at end: make / heaviest-first)')
for n in (2, 4, 6, 8, 12, 16):
    a, amin = makespan(dur_make, n); b, bmin = makespan(dur_lpt, n)
    print(f'{n:>5} {tot/n:>8.1f} {a:>11.1f} {b:>15.1f} {a/b:>6.3f}x   {a-amin:>6.1f} s / {b-bmin:.1f} s')
# the heaviest files and where make started them
order = {o: i for i, (_, _, o) in enumerate(rows)}
print('\nheaviest 8 files: compile time, position in make\'s start order (of %d)' % len(rows))
for s, d, o in sorted(rows, key=lambda r: -r[1])[:8]: print(f'  {d:6.1f} s  #{order[o]+1:5d}  {o}')

# how much the Makefile's own order matters: the same files started alphabetically (how most Makefiles and CMake lists
# are written) and lightest-first (worst case), against heaviest-first
import os
alpha = [d for _, d, _ in sorted(rows, key=lambda r: r[2])]
worst = sorted(dur_make)
print('\nsame files, other start orders (compile-phase wall time):')
print(f'{"cores":>5} {"heaviest first":>15} {"make order":>11} {"alphabetical":>13} {"lightest first":>15}')
for n in (2, 4, 8, 12, 16):
    print(f'{n:>5} {makespan(dur_lpt, n)[0]:>15.1f} {makespan(dur_make, n)[0]:>11.1f} {makespan(alpha, n)[0]:>13.1f} {makespan(worst, n)[0]:>15.1f}')
