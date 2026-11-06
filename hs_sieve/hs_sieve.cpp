// hs_sieve - Harrington's Sets block wrapper + record-length delta pre-filter around the stock "sieve" CLI.
//
// MIT License
// Copyright (c) 2026 Michael Hugh Harrington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Sieve (Sieve of Potentia, github.com/Thor110/Potentia) is AGPL-3.0 by Thor110. It is NOT included here and no
// Sieve source is used: this program only runs the separately built "sieve" executable as an external program
// ("sieve locate FILE --installer OUT.sieve" to pack in its default, size-optimised mode; "sieve install X --to DIR"
// to unpack).
//
// Methods (Michael Hugh Harrington's public MIT methods):
//  1. Harrington's Sets: 64 KB windows labelled by 7 byte statistics (shares of control bytes, letters, digits,
//     spaces, high bytes, zeros, and order-0 entropy), k-means into at most 3 sets, with a window map to restore
//     order. The grouping is kept only if more than one set forms. Each set stream is cut into independent blocks;
//     blocks are packed in parallel, and decoded in parallel on all cores, each piece written straight into its
//     place in the output file (pwrite).
//  2. Record-length detector + delta pre-filter: autocorrelation of byte equality at lags 2..512 finds a fixed
//     record length R. A block is then split into its R byte columns (transpose of its full records); each column
//     is optionally byte-delta coded (chosen by lower order-0 entropy). With --filter auto each block is packed
//     both ways and the smaller is kept, so the filter is only used where it makes the output smaller.
//
// Usage:
//   hs_sieve c IN OUT [--blocks N|0=one block] [--jobs N] [--filter off|on|auto] [--sets 0|3] [--sieve PATH]
//   hs_sieve d IN OUT [--jobs N] [--sieve PATH]
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace std;
typedef vector<uint8_t> Bytes;

static string SIEVE = "sieve";
static string TMP;
static const size_t WIN = 65536;

static Bytes readFile(const string &p) {
    FILE *f = fopen(p.c_str(), "rb"); if (!f) throw runtime_error("cannot open " + p);
    Bytes b; uint8_t buf[1 << 16]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f); return b;
}
static void writeFile(const string &p, const Bytes &b) {
    FILE *f = fopen(p.c_str(), "wb"); if (!f) throw runtime_error("cannot write " + p);
    if (!b.empty() && fwrite(b.data(), 1, b.size(), f) != b.size()) throw runtime_error("write failed " + p); fclose(f);
}
static void run(const string &cmd) { if (system((cmd + " >/dev/null 2>&1").c_str()) != 0) throw runtime_error("failed: " + cmd); }

// ---- the external sieve tool
static Bytes sievePack(const Bytes &in, size_t id) {
    string d = TMP + "/p" + to_string(id); run("rm -rf '" + d + "' && mkdir -p '" + d + "'");
    writeFile(d + "/b", in); run("'" + SIEVE + "' locate '" + d + "/b' --installer '" + d + "/b.sieve'");
    Bytes o = readFile(d + "/b.sieve"); run("rm -rf '" + d + "'"); return o;
}
static Bytes sieveUnpack(const Bytes &in, size_t id) {
    string d = TMP + "/u" + to_string(id); run("rm -rf '" + d + "' && mkdir -p '" + d + "/o'");
    writeFile(d + "/b.sieve", in); run("'" + SIEVE + "' install '" + d + "/b.sieve' --to '" + d + "/o'");
    Bytes o = readFile(d + "/o/b"); run("rm -rf '" + d + "'"); return o;
}

// ---- record-length detector and filter
static double h0(const uint8_t *p, size_t n, size_t step) {
    size_t c[256] = {0}, t = 0; for (size_t i = 0; i < n; i += step) { c[p[i]]++; t++; }
    double h = 0; for (int k = 0; k < 256; k++) if (c[k]) { double q = (double)c[k] / t; h -= q * log2(q); } return h * t;
}
static uint32_t detectRecord(const Bytes &b) {
    size_t n = min<size_t>(b.size(), 4u << 20); if (n < 4096) return 0;
    vector<double> s(513, 0);
    for (size_t L = 2; L <= 512; L++) { size_t m = 0; for (size_t i = L; i < n; i++) m += b[i] == b[i - L]; s[L] = (double)m / (n - L); }
    vector<double> srt(s.begin() + 2, s.end()); sort(srt.begin(), srt.end()); double med = srt[srt.size() / 2];
    size_t best = 0; for (size_t L = 2; L <= 512; L++) if (!best || s[L] > s[best] + 1e-9) best = L;   // smallest lag wins ties
    if (s[best] < med * 1.5 + 0.02) return 0;
    for (size_t d = 2; d < best; d++)   // prefer a divisor lag that scores nearly as well (the fundamental period)
        if (best % d == 0 && s[d] >= s[best] * 0.9) { best = d; break; }
    return (uint32_t)best;
}
// forward: [R columns of the full records, each optionally byte-delta] + tail; flags byte per column at front
static Bytes filterFwd(const Bytes &in, uint32_t R) {
    size_t N = in.size() / R, body = N * R; Bytes out; out.reserve(in.size() + R);
    Bytes col(N), dl(N);
    Bytes flags(R); size_t fpos = 0; out.resize(R); (void)fpos;
    for (uint32_t j = 0; j < R; j++) {
        for (size_t i = 0; i < N; i++) col[i] = in[i * R + j];
        uint8_t prev = 0; for (size_t i = 0; i < N; i++) { dl[i] = (uint8_t)(col[i] - prev); prev = col[i]; }
        bool useD = h0(dl.data(), N, 1) < h0(col.data(), N, 1) * 0.97;
        out[j] = useD; const Bytes &src = useD ? dl : col; out.insert(out.end(), src.begin(), src.end());
    }
    out.insert(out.end(), in.begin() + body, in.end()); return out;
}
static Bytes filterInv(const Bytes &f, uint32_t R, size_t rawLen) {
    size_t N = rawLen / R, body = N * R; if (f.size() != rawLen + R) throw runtime_error("filtered block size mismatch");
    Bytes out(rawLen);
    for (uint32_t j = 0; j < R; j++) {
        const uint8_t *c = f.data() + R + (size_t)j * N; uint8_t prev = 0;
        for (size_t i = 0; i < N; i++) { uint8_t v = f[j] ? (uint8_t)(c[i] + prev) : c[i]; out[i * R + j] = v; prev = v; }
    }
    memcpy(out.data() + body, f.data() + R + body, rawLen - body); return out;
}

// ---- Harrington's Sets labelling
static array<double, 7> feat(const uint8_t *p, size_t n) {
    size_t c[256] = {0}; for (size_t i = 0; i < n; i++) c[p[i]]++;
    double ctl = 0, let = 0, dig = 0, sp = 0, hi = 0, z = c[0], h = 0;
    for (int k = 0; k < 256; k++) {
        if (k < 32 && k != 9 && k != 10 && k != 13 && k != 0) ctl += c[k];
        else if ((k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z')) let += c[k];
        else if (k >= '0' && k <= '9') dig += c[k];
        else if (k == ' ' || k == 9 || k == 10 || k == 13) sp += c[k];
        else if (k >= 128) hi += c[k];
        if (c[k]) { double q = (double)c[k] / n; h -= q * log2(q); }
    }
    return {ctl / n, let / n, dig / n, sp / n, hi / n, z / n, h / 8.0};
}
static double kmeans(const vector<array<double, 7>> &F, int K, vector<int> &a) {
    size_t n = F.size(); vector<array<double, 7>> C; C.push_back(F[0]); a.assign(n, 0);
    auto d2 = [](const array<double, 7> &x, const array<double, 7> &y) { double s = 0; for (int i = 0; i < 7; i++) s += (x[i] - y[i]) * (x[i] - y[i]); return s; };
    while ((int)C.size() < K) { size_t bi = 0; double bd = -1; for (size_t i = 0; i < n; i++) { double m = 1e9; for (auto &c : C) m = min(m, d2(F[i], c)); if (m > bd) { bd = m; bi = i; } } C.push_back(F[bi]); }
    double w = 0;
    for (int it = 0; it < 50; it++) {
        w = 0; for (size_t i = 0; i < n; i++) { int b = 0; double bd = 1e18; for (int k = 0; k < K; k++) { double d = d2(F[i], C[k]); if (d < bd) { bd = d; b = k; } } a[i] = b; w += bd; }
        vector<array<double, 7>> S(K, array<double, 7>{}); vector<size_t> cnt(K, 0);
        for (size_t i = 0; i < n; i++) { cnt[a[i]]++; for (int j = 0; j < 7; j++) S[a[i]][j] += F[i][j]; }
        for (int k = 0; k < K; k++) if (cnt[k]) for (int j = 0; j < 7; j++) C[k][j] = S[k][j] / cnt[k];
    }
    return w;
}
// label windows: K grows from 1 while each extra set removes at least 15% of the remaining scatter
static vector<uint8_t> labelSets(const Bytes &b, int maxK) {
    size_t nw = (b.size() + WIN - 1) / WIN; vector<uint8_t> lab(nw, 0); if (maxK < 2 || nw < 2) return lab;
    vector<array<double, 7>> F(nw); for (size_t w = 0; w < nw; w++) F[w] = feat(b.data() + w * WIN, min(WIN, b.size() - w * WIN));
    vector<int> a; double w1 = kmeans(F, 1, a), wPrev = w1; vector<int> best = a;
    for (int k = 2; k <= maxK && wPrev > 1e-12; k++) { vector<int> ak; double wk = kmeans(F, k, ak); if (wPrev - wk < 0.15 * wPrev) break; best = ak; wPrev = wk; }
    for (size_t w = 0; w < nw; w++) lab[w] = (uint8_t)best[w]; return lab;
}

// ---- container
static void put64(Bytes &o, uint64_t v) { for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> (8 * i))); }
static uint64_t get64(const Bytes &b, size_t &p) { if (p + 8 > b.size()) throw runtime_error("truncated"); uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)b[p + i] << (8 * i); p += 8; return v; }
struct Block { uint64_t rawLen = 0; uint8_t filtered = 0; Bytes z; };
static void parallelFor(size_t n, int jobs, const function<void(size_t)> &f) {
    atomic<size_t> next(0); vector<string> err; vector<thread> T; bool fail = false;
    for (int t = 0; t < jobs; t++) T.emplace_back([&] { size_t i; while ((i = next++) < n) { try { f(i); } catch (exception &e) { fprintf(stderr, "block %zu: %s\n", i, e.what()); fail = true; } } });
    for (auto &t : T) t.join(); if (fail) throw runtime_error("a block failed");
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "hs_sieve c IN OUT [--blocks N] [--jobs N] [--filter off|on|auto] [--sets 0|3] [--sieve PATH]\nhs_sieve d IN OUT [--jobs N] [--sieve PATH]\n"); return 2; }
    string mode = argv[1], inP = argv[2], outP = argv[3];
    int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 1; long nblocks = -1; string filt = "auto"; int maxK = 3;
    if (getenv("SIEVE")) SIEVE = getenv("SIEVE");
    for (int i = 4; i + 1 < argc; i += 2) { string k = argv[i], v = argv[i + 1];
        if (k == "--blocks") nblocks = atol(v.c_str()); else if (k == "--jobs") jobs = atoi(v.c_str()); else if (k == "--filter") filt = v;
        else if (k == "--sets") maxK = atoi(v.c_str()); else if (k == "--sieve") SIEVE = v; else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; } }
    char tbuf[] = "/tmp/hs_sieve_XXXXXX"; if (!mkdtemp(tbuf)) { perror("mkdtemp"); return 1; } TMP = tbuf;
    auto t0 = chrono::steady_clock::now();
    try {
        if (mode == "c") {
            Bytes in = readFile(inP); uint64_t n = in.size();
            vector<uint8_t> lab = labelSets(in, maxK); int nsets = 0; for (auto l : lab) nsets = max(nsets, l + 1);
            bool useSets = nsets > 1; if (!useSets) lab.assign(lab.size(), 0);
            // set streams in set order (window order kept inside each set)
            vector<Bytes> streams(useSets ? nsets : 1);
            if (useSets) { for (size_t w = 0; w < lab.size(); w++) { size_t s = w * WIN, e = min<size_t>(n, s + WIN); streams[lab[w]].insert(streams[lab[w]].end(), in.begin() + s, in.begin() + e); } }
            else streams[0] = in;
            uint32_t R = filt == "off" ? 0 : detectRecord(in);
            if (nblocks < 0) nblocks = jobs; if (nblocks < 1) nblocks = 1;
            // blocks: each stream cut into pieces proportional to its size, at least 1 MB, aligned to R when R>0
            vector<Bytes> raw; vector<uint8_t> blkSet;
            for (size_t s = 0; s < streams.size(); s++) { const Bytes &S = streams[s]; if (S.empty()) continue;
                size_t k = max<size_t>(1, (size_t)llround((double)nblocks * S.size() / n)); k = min<size_t>(k, max<size_t>(1, S.size() >> 20));
                size_t step = (S.size() + k - 1) / k; if (R) step = (step + R - 1) / R * R;
                for (size_t p = 0; p < S.size(); p += step) { raw.emplace_back(S.begin() + p, S.begin() + min(S.size(), p + step)); blkSet.push_back((uint8_t)s); } }
            vector<Block> B(raw.size());
            parallelFor(raw.size(), jobs, [&](size_t i) {
                B[i].rawLen = raw[i].size();
                if (R && filt != "off") { Bytes fz = sievePack(filterFwd(raw[i], R), i);
                    if (filt == "on") { B[i].z = fz; B[i].filtered = 1; return; }
                    Bytes rz = sievePack(raw[i], i + 100000);
                    if (fz.size() < rz.size()) { B[i].z = fz; B[i].filtered = 1; } else B[i].z = rz; return; }
                B[i].z = sievePack(raw[i], i); });
            Bytes o = {'H', 'S', 'S', 'V', 1}; put64(o, n); put64(o, R); put64(o, useSets ? lab.size() : 0);
            if (useSets) o.insert(o.end(), lab.begin(), lab.end());
            put64(o, B.size()); for (size_t i = 0; i < B.size(); i++) { o.push_back(blkSet[i]); o.push_back(B[i].filtered); put64(o, B[i].rawLen); put64(o, B[i].z.size()); }
            for (auto &b : B) o.insert(o.end(), b.z.begin(), b.z.end());
            writeFile(outP, o);
            size_t nf = 0; for (auto &b : B) nf += b.filtered;
            printf("%llu -> %zu bytes, record length %u, sets %d, blocks %zu (%zu filtered), jobs %d, %.2f s\n", (unsigned long long)n, o.size(), R, useSets ? nsets : 1, B.size(), nf, jobs,
                   chrono::duration<double>(chrono::steady_clock::now() - t0).count());
        } else if (mode == "d") {
            Bytes c = readFile(inP); size_t p = 5; if (c.size() < 5 || memcmp(c.data(), "HSSV\1", 5)) throw runtime_error("not an hs_sieve file");
            uint64_t n = get64(c, p), R = get64(c, p), nw = get64(c, p);
            if (nw && (nw != (n + WIN - 1) / WIN || p + nw > c.size())) throw runtime_error("bad window map"); vector<uint8_t> lab(c.begin() + p, c.begin() + p + nw); p += nw;
            uint64_t nb = get64(c, p); if (nb > c.size()) throw runtime_error("bad block count");
            vector<uint8_t> bs(nb), bf(nb); vector<uint64_t> rl(nb), zl(nb), zo(nb);
            for (uint64_t i = 0; i < nb; i++) { if (p + 2 > c.size()) throw runtime_error("truncated"); bs[i] = c[p++]; bf[i] = c[p++]; rl[i] = get64(c, p); zl[i] = get64(c, p); }
            uint64_t tot = 0; for (uint64_t i = 0; i < nb; i++) { zo[i] = p + tot; tot += zl[i]; if (zl[i] > c.size() || tot > c.size() - p) throw runtime_error("truncated blocks"); }
            // where each block's bytes go: list of (stream offset -> file offset) runs
            int nsets = 1; for (auto l : lab) nsets = max(nsets, l + 1);
            vector<vector<pair<uint64_t, uint64_t>>> setWins(nsets);   // per set: (file offset, length) in stream order
            if (nw) for (uint64_t w = 0; w < nw; w++) setWins[lab[w]].push_back({w * WIN, min<uint64_t>(WIN, n - w * WIN)});
            else setWins[0].push_back({0, n});
            vector<uint64_t> bstart(nb); { vector<uint64_t> acc(nsets, 0); for (uint64_t i = 0; i < nb; i++) { if (bs[i] >= nsets) throw runtime_error("bad set"); bstart[i] = acc[bs[i]]; acc[bs[i]] += rl[i]; } }
            int fd = open(outP.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0 || ftruncate(fd, (off_t)n) != 0) throw runtime_error("cannot create output");
            parallelFor(nb, jobs, [&](size_t i) {
                Bytes z(c.begin() + zo[i], c.begin() + zo[i] + zl[i]); Bytes d = sieveUnpack(z, i);
                if (bf[i]) d = filterInv(d, (uint32_t)R, rl[i]); if (d.size() != rl[i]) throw runtime_error("block size mismatch");
                uint64_t s = bstart[i], left = d.size(), dp = 0, acc = 0;
                for (auto &wn : setWins[bs[i]]) { if (acc + wn.second <= s) { acc += wn.second; continue; } if (!left) break;
                    uint64_t off = s > acc ? s - acc : 0, take = min<uint64_t>(wn.second - off, left);
                    if (pwrite(fd, d.data() + dp, take, (off_t)(wn.first + off)) != (ssize_t)take) throw runtime_error("pwrite failed");
                    dp += take; left -= take; s += take; acc += wn.second; } });
            close(fd);
            printf("%zu -> %llu bytes, blocks %llu, jobs %d, %.2f s\n", c.size(), (unsigned long long)n, (unsigned long long)nb, jobs, chrono::duration<double>(chrono::steady_clock::now() - t0).count());
        } else { fprintf(stderr, "mode must be c or d\n"); return 2; }
    } catch (exception &e) { fprintf(stderr, "error: %s\n", e.what()); run("rm -rf '" + TMP + "'"); return 1; }
    run("rm -rf '" + TMP + "'"); return 0;
}
