#!/usr/bin/env python3
"""Ablation runner for hs_zstd 1.3-ablation: Harrington Sets on and off, and three raw rules.

For every file, block size and variant it compresses once, then decodes several rounds with --flush (output forced
to disk before the clock stops), checks every round trip by SHA-256, and records size, compress time, median decode
time and (if psutil is installed) peak memory. pzstd and single-thread zstd are added when found on PATH.

Usage:  python run_ablation.py --exe ./hs_zstd FILE [FILE ...] [--jobs 12] [--rounds 5] [--blocks 4,16]
Writes ablation_results.csv and prints a summary table (sizes relative to --sets 0, i.e. plain block splitting).
"""
import argparse, csv, hashlib, os, shutil, statistics, subprocess, sys, tempfile, threading, time
VARIANTS = [("Sets (published 1.2 behaviour)", []), ("--sets 0 (no Sets, raw off)", ["--sets", "0"]),
            ("Sets, --raw test", ["--raw", "test"]), ("Sets, --raw off", ["--raw", "off"]),
            ("--sets 0 --raw test", ["--sets", "0", "--raw", "test"]),
            ("Sets, --smooth safe", ["--smooth", "safe"]), ("Sets, --smooth safe --raw off", ["--smooth", "safe", "--raw", "off"])]
def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""): h.update(b)
    return h.hexdigest()
def timed(cmd):
    """Run cmd; return (wall seconds, peak RSS MB or None)."""
    peak = [None]
    try:
        import psutil
    except ImportError:
        psutil = None
    t0 = time.perf_counter(); p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if psutil:
        def poll():
            try:
                pp = psutil.Process(p.pid); m = 0
                while p.poll() is None:
                    try: m = max(m, pp.memory_info().rss)
                    except psutil.Error: break
                    time.sleep(0.01)
                peak[0] = m / 2**20
            except psutil.Error: pass
        th = threading.Thread(target=poll); th.start()
    _, err = p.communicate(); dt = time.perf_counter() - t0
    if psutil: th.join()
    if p.returncode: raise RuntimeError(f"{' '.join(cmd)} failed: {err.decode(errors='replace')}")
    return dt, peak[0]
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("files", nargs="+"); ap.add_argument("--exe", default="hs_zstd")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4); ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--blocks", default="4,16"); ap.add_argument("--tmp", default=None); a = ap.parse_args()
    blocks = [int(b) for b in a.blocks.split(",")]; tmp = a.tmp or tempfile.mkdtemp(prefix="hsab_")
    zstd = shutil.which("zstd"); pzstd = shutil.which("pzstd"); rows = []
    for f in a.files:
        n = os.path.getsize(f); ref = sha(f); print(f"\n{os.path.basename(f)}: {n:,} bytes", flush=True)
        jobs = []
        for b in blocks:
            for name, opt in VARIANTS:
                jobs.append((f"{name}, {b} MB blocks", [a.exe, "c", f, None, "--jobs", str(a.jobs), "--block-mb", str(b)] + opt,
                             lambda c, o: [a.exe, "d", c, o, "--jobs", str(a.jobs), "--flush"], b, opt))
        if zstd: jobs.append(("zstd -3, one thread", [zstd, "-3", "-q", "-f", f, "-o", None], lambda c, o: [zstd, "-d", "-q", "-f", c, "-o", o], None, None))
        if pzstd: jobs.append((f"pzstd -3 -p {a.jobs}", [pzstd, "-3", "-q", "-f", "-p", str(a.jobs), f, "-o", None], lambda c, o: [pzstd, "-d", "-q", "-f", "-p", str(a.jobs), c, "-o", o], None, None))
        for name, ccmd, dfun, b, opt in jobs:
            comp = os.path.join(tmp, "c.bin"); outp = os.path.join(tmp, "o.bin")
            ccmd = [comp if x is None else x for x in ccmd]; ct, cmem = timed(ccmd); size = os.path.getsize(comp)
            dts, dmem = [], 0
            for r in range(a.rounds):
                if os.path.exists(outp): os.remove(outp)
                dt, m = timed(dfun(comp, outp)); dts.append(dt); dmem = max(dmem, m or 0)
                if sha(outp) != ref: raise RuntimeError(f"round trip failed for {name}")
            rows.append(dict(file=os.path.basename(f), variant=name, block_mb=b or "", bytes=size, per_million=round(1e6 * size / n),
                             compress_s=round(ct, 3), decode_median_s=round(statistics.median(dts), 3), decode_all_s=" ".join(f"{x:.3f}" for x in dts),
                             decode_peak_mb=round(dmem, 1) if dmem else "", decode_MBps=round(n / 1e6 / statistics.median(dts), 1)))
            print(f"  {name:45} {size:>14,}  decode median {statistics.median(dts):7.3f} s", flush=True)
    with open("ablation_results.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    print("\nSizes relative to --sets 0 at the same block size (negative = smaller):")
    for f in dict.fromkeys(r["file"] for r in rows):
        for b in blocks:
            base = next(r for r in rows if r["file"] == f and r["block_mb"] == b and r["variant"].startswith("--sets 0 (no"))
            for r in rows:
                if r["file"] == f and r["block_mb"] == b:
                    print(f"  {f:28} {r['variant']:45} {100 * (r['bytes'] - base['bytes']) / base['bytes']:+7.2f}%   decode {r['decode_median_s']:.3f} s (base {base['decode_median_s']:.3f} s)")
    print("\nWrote ablation_results.csv")
if __name__ == "__main__":
    main()
