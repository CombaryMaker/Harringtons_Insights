// hs_lzip.cpp (version 1.0, 8 Nov 2026) - Harrington's Sets + lzip, multi-core. Michael Hugh Harrington, MIT License.
// lzlib 1.16 (Antonio Diaz Diaz, 2-clause BSD) is linked in unchanged; every block is a standard lzip member.
//
//   --sets 0  : plain blocks in file order -> the output is a STANDARD multi-member .lz file (stock lzip and plzip read
//               it; plzip and hs_lzip decode its members on all cores at once).
//   --sets 3  : (default) Harrington's Sets: the file is labelled in 64 KB windows (the same 7 byte-statistics features
//               and k-means as hs_zstd 1.3, up to 3 sets, safe run smoothing), each set is cut into blocks, every block is
//               compressed on its own core as one lzip member. Output: "HSL1" header (the set map and block table) +
//               the lzip members. hs_lzip puts every block back in place on all cores at once.
// Levels 0-9 are lzip's own (dictionary size and match length limit as in lzip); the dictionary of each member is
// reduced to its block length, as lzip does for small files.
// Usage: hs_lzip c IN OUT [--level 0-9] [--block-mb B] [--jobs N] [--sets 0|1|2|3]      (defaults: 6, 16, all cores, 3)
//        hs_lzip d IN OUT [--jobs N]          (reads HSL1 files and standard .lz files, single- or multi-member)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <atomic>
#include <algorithm>
#include <stdexcept>
extern "C" {
#include "lzlib.h"
}
using namespace std;
static const size_t WIN = 65536;

static vector<uint8_t> readf(const char *fn) { FILE *f = fopen(fn, "rb"); if (!f) throw runtime_error(string("cannot open ") + fn);
    vector<uint8_t> d; uint8_t b[1 << 16]; size_t m; while ((m = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + m); fclose(f); return d; }
static void writef(const char *fn, const uint8_t *p, size_t n) { FILE *f = fopen(fn, "wb"); if (!f) throw runtime_error(string("cannot write ") + fn);
    if (n && fwrite(p, 1, n, f) != n) throw runtime_error("write failed"); if (fclose(f)) throw runtime_error("write failed"); }

// ---- lzip member encode / decode (lzlib) ----
static const int DICT[10] = {65535, 1 << 20, 3 << 19, 1 << 21, 3 << 20, 1 << 22, 1 << 23, 1 << 24, 3 << 23, 1 << 25};
static const int MML[10] = {16, 5, 6, 8, 12, 20, 36, 68, 132, 273};
static vector<uint8_t> enc(const uint8_t *in, size_t n, int level) {
    int dict = DICT[level]; if (level > 0 && (size_t)dict > n) dict = (int)max<size_t>(n, (size_t)LZ_min_dictionary_size());
    LZ_Encoder *e = LZ_compress_open(dict, MML[level], INT64_MAX); if (!e || LZ_compress_errno(e) != LZ_ok) throw runtime_error("lzlib: cannot start encoder");
    vector<uint8_t> out(n / 4 + 64); size_t ip = 0, op = 0;
    for (;;) { int r = LZ_compress_write(e, in + ip, (int)min<size_t>(INT_MAX, n - ip)); if (r < 0) throw runtime_error("lzlib: encode error"); ip += r;
        if (ip >= n) LZ_compress_finish(e);
        if (op == out.size()) out.resize(out.size() * 2);
        r = LZ_compress_read(e, out.data() + op, (int)min<size_t>(INT_MAX, out.size() - op)); if (r < 0) throw runtime_error("lzlib: encode error"); op += r;
        if (LZ_compress_finished(e) == 1) break; }
    LZ_compress_close(e); out.resize(op); return out;
}
static void dec_into(const uint8_t *in, size_t n, uint8_t *dst, size_t want) {
    LZ_Decoder *d = LZ_decompress_open(); if (!d || LZ_decompress_errno(d) != LZ_ok) throw runtime_error("lzlib: cannot start decoder");
    size_t ip = 0, op = 0;
    for (;;) { int r = LZ_decompress_write(d, in + ip, (int)min<size_t>(INT_MAX, n - ip)); if (r < 0) throw runtime_error("lzlib: damaged data"); ip += r;
        if (ip >= n) LZ_decompress_finish(d);
        uint8_t spill[1]; uint8_t *to = op < want ? dst + op : spill; int room = op < want ? (int)min<size_t>(INT_MAX, want - op) : 1;
        r = LZ_decompress_read(d, to, room); if (r < 0) throw runtime_error("lzlib: damaged data"); if (op >= want && r > 0) throw runtime_error("member longer than its table entry"); op += r;
        if (LZ_decompress_finished(d) == 1) break; }
    LZ_decompress_close(d); if (op != want) throw runtime_error("member shorter than its table entry");
}
template <class Fn> static void par(size_t n, int T, Fn fn) { atomic<size_t> nx{0}; string err; vector<thread> th;
    for (int t = 0; t < T; t++) th.emplace_back([&] { try { for (size_t k; (k = nx++) < n;) fn(k); } catch (exception &e) { err = e.what(); } });
    for (auto &x : th) x.join(); if (!err.empty()) throw runtime_error(err); }

// ---- labelling (same rules as hs_zstd 1.3: 7 features, k-means up to 3 sets, safe smoothing, no raw windows) ----
static void features(const uint8_t *c, size_t L, array<double, 7> &F) {
    double h[256] = {0}; for (size_t i = 0; i < L; i++) h[c[i]]++; double e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double p = h[i] / L; e -= p * log2(p); }
    double alpha = 0, digit = 0, high = 0, ctl = 0; for (int i = 65; i < 91; i++) alpha += h[i]; for (int i = 97; i < 123; i++) alpha += h[i];
    for (int i = 48; i < 58; i++) digit += h[i]; for (int i = 128; i < 256; i++) high += h[i]; for (int i = 0; i < 32; i++) ctl += h[i]; ctl += h[127] - h[10] - h[13] - h[9];
    double space = h[32] + h[10] + h[13] + h[9]; F = {ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8};
}
static double d2f(const array<double, 7> &a, const array<double, 7> &b) { double s = 0; for (int k = 0; k < 7; k++) s += (a[k] - b[k]) * (a[k] - b[k]); return s; }
static vector<int> labels(const vector<array<double, 7>> &F, int maxK) {
    size_t m = F.size(); vector<int> lab(m, 0); int K = m >= 48 ? 3 : (m >= 16 ? 2 : 1); K = min(K, max(1, maxK)); if (K <= 1) return lab;
    vector<array<double, 7>> C(1, F[0]);
    for (int k = 1; k < K; k++) { size_t best = 0; double bd = -1; for (size_t j = 0; j < m; j++) { double d = 1e300; for (auto &c : C) d = min(d, d2f(F[j], c)); if (d > bd) { bd = d; best = j; } } C.push_back(F[best]); }
    vector<int> a(m, 0);
    for (int it = 0; it < 20; it++) {
        for (size_t j = 0; j < m; j++) { int b = 0; double bd = d2f(F[j], C[0]); for (int k = 1; k < K; k++) { double d = d2f(F[j], C[k]); if (d < bd) { bd = d; b = k; } } a[j] = b; }
        for (int k = 0; k < K; k++) { array<double, 7> s{}; size_t c = 0; for (size_t j = 0; j < m; j++) if (a[j] == k) { for (int q = 0; q < 7; q++) s[q] += F[j][q]; c++; } if (c) for (int q = 0; q < 7; q++) C[k][q] = s[q] / c; } }
    vector<size_t> cnt(K, 0); for (int x : a) cnt[x]++; int big = (int)(max_element(cnt.begin(), cnt.end()) - cnt.begin());
    for (size_t j = 0; j < m; j++) lab[j] = cnt[a[j]] * WIN < (1u << 20) ? big : a[j];   // sets under 1 MB join the biggest
    for (int pass = 0; pass < 2; pass++) { size_t i = 0; while (i < m) { size_t j = i; while (j < m && lab[j] == lab[i]) j++; if (j - i < 8 && i > 0) for (size_t q = i; q < j; q++) lab[q] = lab[i - 1]; i = j; } }
    return lab;
}
static void put32(vector<uint8_t> &o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((uint8_t)(v >> 8 * i)); }
static void put64(vector<uint8_t> &o, uint64_t v) { for (int i = 0; i < 8; i++) o.push_back((uint8_t)(v >> 8 * i)); }
static uint64_t get(const uint8_t *p, int n) { uint64_t v = 0; for (int i = n - 1; i >= 0; i--) v = v << 8 | p[i]; return v; }

static void compress(const char *inf, const char *outf, int level, long bmb, int T, int maxK) {
    vector<uint8_t> in = readf(inf); size_t n = in.size(), nw = (n + WIN - 1) / WIN, B = (size_t)bmb << 20;
    vector<int> lab(nw, 0);
    if (maxK > 1 && nw > 0) { vector<array<double, 7>> F(nw); par(nw, T, [&](size_t i) { size_t L = min(WIN, n - i * WIN); features(in.data() + i * WIN, L, F[i]); }); lab = labels(F, maxK); }
    bool sets = false; for (int x : lab) if (x != 0) sets = true;
    // gather each set's windows in file order, cut into blocks of about B bytes
    vector<int> setsUsed; for (int x : lab) if (find(setsUsed.begin(), setsUsed.end(), x) == setsUsed.end()) setsUsed.push_back(x); sort(setsUsed.begin(), setsUsed.end());
    vector<uint8_t> gathered; gathered.reserve(sets ? n : 0); struct Blk { int s; size_t off, len; }; vector<Blk> blk;
    const uint8_t *src = in.data();
    if (sets) { for (int s : setsUsed) { size_t st = gathered.size(); for (size_t i = 0; i < nw; i++) if (lab[i] == s) gathered.insert(gathered.end(), in.begin() + i * WIN, in.begin() + min(n, (i + 1) * WIN));
            size_t L = gathered.size() - st, nb = max<size_t>(1, (L + B - 1) / B), p = (L + nb - 1) / nb; for (size_t k = 0; k < nb; k++) { size_t a = min(L, k * p), b = min(L, (k + 1) * p); if (b > a || L == 0) blk.push_back({s, st + a, b - a}); } }
        src = gathered.data(); }
    else { size_t nb = max<size_t>(1, (n + B - 1) / B), p = (n + nb - 1) / nb; for (size_t k = 0; k < nb; k++) blk.push_back({0, min(n, k * p), min(n, (k + 1) * p) - min(n, k * p)}); }
    vector<vector<uint8_t>> z(blk.size()); par(blk.size(), T, [&](size_t k) { z[k] = enc(src + blk[k].off, blk[k].len, level); });
    vector<uint8_t> out;
    if (sets) { out.insert(out.end(), {'H', 'S', 'L', '1'}); put64(out, n); vector<pair<int, uint32_t>> R;
        for (size_t i = 0; i < nw;) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; R.push_back({lab[i], (uint32_t)(j - i)}); i = j; }
        put32(out, (uint32_t)R.size()); for (auto &r : R) { out.push_back((uint8_t)r.first); put32(out, r.second); }
        put32(out, (uint32_t)blk.size()); for (size_t k = 0; k < blk.size(); k++) { out.push_back((uint8_t)blk[k].s); put64(out, blk[k].len); put64(out, z[k].size()); } }
    for (auto &m : z) out.insert(out.end(), m.begin(), m.end());
    writef(outf, out.data(), out.size());
    printf("%zu -> %zu bytes; %s, %zu block(s), lzip level %d\n", n, out.size(), sets ? "Harrington's Sets (HSL1)" : "plain blocks (standard multi-member .lz)", blk.size(), level);
}
static void decompress(const char *inf, const char *outf, int T) {
    vector<uint8_t> in = readf(inf); size_t m = in.size();
    if (m >= 4 && !memcmp(in.data(), "HSL1", 4)) {
        const uint8_t *p = in.data() + 4; size_t n = get(p, 8); p += 8; uint32_t nr = (uint32_t)get(p, 4); p += 4; vector<pair<int, uint32_t>> R(nr);
        for (auto &r : R) { r.first = p[0]; r.second = (uint32_t)get(p + 1, 4); p += 5; }
        uint32_t nb = (uint32_t)get(p, 4); p += 4; struct Blk { int s; size_t len, clen, coff, goff; }; vector<Blk> b(nb);
        for (auto &x : b) { x.s = p[0]; x.len = get(p + 1, 8); x.clen = get(p + 9, 8); p += 17; }
        size_t co = p - in.data(), go = 0; for (auto &x : b) { x.coff = co; co += x.clen; x.goff = go; go += x.len; if (co > m || go > n) throw runtime_error("damaged file"); }
        vector<uint8_t> g(n), out(n); par(nb, T, [&](size_t k) { dec_into(in.data() + b[k].coff, b[k].clen, g.data() + b[k].goff, b[k].len); });
        // put the windows back: each set's data is in g in window order, sets in ascending label order
        vector<int> setsUsed; for (auto &r : R) if (find(setsUsed.begin(), setsUsed.end(), r.first) == setsUsed.end()) setsUsed.push_back(r.first); sort(setsUsed.begin(), setsUsed.end());
        vector<size_t> base(256, 0); { size_t acc = 0; for (int s : setsUsed) { base[s] = acc; size_t w = 0; for (auto &r : R) { for (uint32_t i = 0; i < r.second; i++, w++) if (r.first == s) acc += min(WIN, n - w * WIN); } } }
        size_t w = 0; for (auto &r : R) for (uint32_t i = 0; i < r.second; i++, w++) { size_t L = min(WIN, n - w * WIN); memcpy(out.data() + w * WIN, g.data() + base[r.first], L); base[r.first] += L; }
        writef(outf, out.data(), n); return; }
    // standard .lz (one or more members): find the members from the end (each trailer ends with the member size)
    struct Mem { size_t off, len, dlen, doff; }; vector<Mem> M; size_t end = m;
    while (end > 0) { if (end < 26) throw runtime_error("not an lzip file"); size_t ms = get(in.data() + end - 8, 8), ds = get(in.data() + end - 16, 8);
        if (ms > end || ms < 26 || memcmp(in.data() + end - ms, "LZIP", 4)) throw runtime_error("not an lzip file (or damaged)"); M.push_back({end - ms, ms, ds, 0}); end -= ms; }
    reverse(M.begin(), M.end()); size_t tot = 0; for (auto &x : M) { x.doff = tot; tot += x.dlen; }
    vector<uint8_t> out(tot); par(M.size(), T, [&](size_t k) { dec_into(in.data() + M[k].off, M[k].len, out.data() + M[k].doff, M[k].dlen); });
    writef(outf, out.data(), tot);
}
int main(int argc, char **argv) {
    try {
        if (argc < 4) { fprintf(stderr, "usage: hs_lzip c IN OUT [--level 0-9] [--block-mb B] [--jobs N] [--sets 0-3] | hs_lzip d IN OUT [--jobs N]\n"); return 2; }
        int level = 6, T = (int)max(1u, thread::hardware_concurrency()), sets = 3; long bmb = 16;
        for (int i = 4; i + 1 < argc; i += 2) { string k = argv[i]; long v = atol(argv[i + 1]);
            if (k == "--level") level = (int)min(9L, max(0L, v)); else if (k == "--block-mb") bmb = max(1L, v); else if (k == "--jobs") T = (int)max(1L, v); else if (k == "--sets") sets = (int)min(3L, max(0L, v)); else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; } }
        if (!strcmp(argv[1], "c")) compress(argv[2], argv[3], level, bmb, T, sets); else if (!strcmp(argv[1], "d")) decompress(argv[2], argv[3], T); else return 2;
    } catch (exception &e) { fprintf(stderr, "hs_lzip: %s\n", e.what()); return 1; }
    return 0;
}
