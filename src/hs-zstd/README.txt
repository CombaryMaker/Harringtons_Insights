hs_zstd - Harrington Sets + zstd
hs_zstd.cpp (1.3; --raw off and --smooth safe defaults after Andrew Goodwin's review, test pack in ablation-1.3/) compresses a file as Harrington Sets blocks of standard zstd frames and decodes every block on its own core straight into a memory-mapped output. hszcorp.cpp and pzrace.cpp are the test harnesses; results-*.txt are the raw desktop results. Build: zstd lib objects + g++ -O2 -std=c++17 -pthread hs_zstd.cpp libzstd.a -o hs_zstd. Full write-up: docs/forum/hs_zstd.txt.

MIT License. Copyright (c) 2026 Michael Hugh Harrington. Full text at the end of this file.

RESULTS
2 GB (Silesia repeated), 3 decode rounds
+------------------------+----------+-------------+------------------------+---------+---------------+
| Tool                   | bits     | compress s  | decode s (3 rounds)    | median  | decoder peak  |
+------------------------+----------+-------------+------------------------+---------+---------------+
| zstd (1 core)          | 313,334  | 7.1         | 1.70 / 1.67 / 1.67     | 1.67    | 15 MB         |
| zstd -T0               | 313,334  | 1.7         | (same format, 1 core)  | -       | -             |
| pzstd -p 12            | 314,262  | 1.4         | 1.18 / 0.76 / 0.94     | 0.94    | 160 MB        |
| hs_zstd, 16 MB blocks  | 313,652  | 1.6         | 0.72 / 0.66 / 0.69     | 0.69    | 217 MB        |
| hs_zstd, 4 MB blocks   | 314,561  | 1.5         | 0.70 / 0.63 / 0.62     | 0.63    | 116 MB        |
+------------------------+----------+-------------+------------------------+---------+---------------+
hs_zstd 4 MB vs pzstd: 1.50x faster decode, 116 vs 160 MB, 299 bits per million larger.
hs_zstd 16 MB vs pzstd: smaller and 1.36x faster.

10 GB (Silesia repeated), 2 decode rounds only; noisy because the output fills most of RAM. The 2 GB table is
the evidence; the 10 GB table is shown for completeness.
+------------------------+----------+-------------+------------------+---------+---------------+
| Tool                   | bits     | compress s  | decode s (2)     | median  | decoder peak  |
+------------------------+----------+-------------+------------------+---------+---------------+
| zstd (1 core)          | 314,353  | 32.3        | 9.58 / 14.33     | 11.95   | 15 MB         |
| pzstd -p 12            | 315,259  | 7.4         | 5.21 / 8.54      | 6.87    | 164 MB        |
| hs_zstd, 16 MB blocks  | 314,600  | 7.7         | 5.94 / 3.91      | 4.93    | 293 MB        |
| hs_zstd, 4 MB blocks   | 315,589  | 7.5         | 3.81 / 4.20      | 4.00    | 135 MB        |
+------------------------+----------+-------------+------------------+---------+---------------+

20 GB, hs_zstd 1.0, 16 MB blocks, 3 rounds: zstd median 28.51 s, hs_zstd 10.71 s (2.66x); bits 314,200 vs
314,437; decoder peak 15 vs 313-318 MB.

The 12 Silesia files one at a time (211,938,580 bytes), summed per-file median decode of 3 runs
+------------------+----------+-------------+---------------------+------------------+
| Tool             | bits     | compress s  | decode s (12 files) | highest peak MB  |
+------------------+----------+-------------+---------------------+------------------+
| LZ4 default      | 476,244  | 0.9         | 1.166               | 45               |
| zstd default     | 313,995  | 1.3         | 1.144               | 15               |
| hs_zstd 16 MB    | 314,273  | 1.0         | 0.575               | 43               |
| hs_zstd 4 MB     | 314,933  | 0.8         | 0.482               | 52               |
+------------------+----------+-------------+---------------------+------------------+
(webster's LZ4 and zstd decodes took 0.52 s against about 0.05 s for every other file; without it the four
totals are 0.645 / 0.627 / 0.512 / 0.433 s.)

WHAT WAS TRIED
  step                                                     outcome
  hs_wrap 1.3 (temp files) + zstd, 10 GB                   decompress 0.85x of plain zstd: FAILED (temp-file writes)
  hs_wrap 1.4 direct decode (pipes, one writer)            3.25x on gzip, but zstd through pipes still lost to pzstd: PARTIAL
  hs_zstd 1.0: zstd linked in, mmap output, per-core CRC   2.43x plain zstd, beat pzstd on decode: SUCCESS
  hs_zstd 1.1: single-call decode for contiguous blocks    same files, less memory (217 -> 116 MB at 4 MB blocks): SUCCESS
  hs_zstd 1.2: seek checks, context cleanup                same files; damaged-file test 90 rejections / 0 wrong outputs: SUCCESS
  10 GB and 20 GB runs                                     faster, but noisy once output exceeds RAM: PARTIAL (quote 2 GB)

MIT License. Copyright (c) 2026 Michael Hugh Harrington.

CODE: hs_zstd.cpp version 1.2, in full

// hs_zstd.cpp (version 1.2) - Harrington Sets + zstd, one program (zstd built in, no external tools, no temporary files).
// Original method (Harrington Sets) by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
// zstd (Meta, BSD licence) is linked in unchanged; every block inside is a standard zstd frame.
//
// Compress: the file is labelled in 64 KB windows (entropy exclusion above 7.99 bits/byte, k-means into at most 3
// sets, run smoothing - the same rules as hs_wrap), each set is cut into blocks of B MB, every block is
// compressed by zstd on its own core, raw (incompressible) windows are stored as they are.
// Decompress: the output file is mapped into memory and every core decodes its blocks STRAIGHT INTO THEIR FINAL
// PLACE in the output (zstd streaming output pointed at each piece of the output in turn) - no pipes, no
// temporary files, no single writer, no extra copy. CRC-32 is computed per piece on the worker cores and joined.
// 1.1: a block whose output is one contiguous piece is decoded in one call straight into place (no zstd stream
// buffer, which for a 16 MB single-segment frame is about 16 MB per core); split blocks still use streaming.
// 1.2: hardened container checks (full header size before any offset arithmetic, overflow-safe block sums,
// impossible lengths rejected), checked seeks, zstd contexts freed on every exit path, and a no-progress guard in
// the streaming decoder, so a damaged frame is rejected instead of looping.
// Usage: hs_zstd c IN OUT [--level L] [--jobs N] [--block-mb B]      hs_zstd d IN OUT [--jobs N]
// Build: zstd lib objects + g++ -O2 -std=c++17 -pthread hs_zstd.cpp libzstd.a -o hs_zstd
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include "zstd.h"
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
#define FSEEK _fseeki64
#else
#define FSEEK fseeko
#endif
static const size_t W = 65536; static const int RAW = 255;

// ---- CRC-32 (zlib polynomial), slicing by 8 ----
static uint32_t T8[8][256];
static void crc_init() {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T8[0][i] = c; }
    for (int t = 1; t < 8; t++) for (int i = 0; i < 256; i++) T8[t][i] = (T8[t - 1][i] >> 8) ^ T8[0][T8[t - 1][i] & 255];
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, size_t n) {   // c = running value (start 0)
    c = ~c;
    while (n >= 8) {
        uint32_t a = c ^ (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24), b = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        c = T8[7][a & 255] ^ T8[6][(a >> 8) & 255] ^ T8[5][(a >> 16) & 255] ^ T8[4][a >> 24] ^ T8[3][b & 255] ^ T8[2][(b >> 8) & 255] ^ T8[1][(b >> 16) & 255] ^ T8[0][b >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = T8[0][(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

// ---- small file helpers ----
struct F { FILE *f = nullptr; F(const string &p, const char *m) { f = fopen(p.c_str(), m); if (!f) throw runtime_error("cannot open " + p); setvbuf(f, nullptr, _IOFBF, 1 << 20); } ~F() { if (f) fclose(f); } };
static void wr(FILE *f, const void *p, size_t n) { if (n && fwrite(p, 1, n, f) != n) throw runtime_error("write failed (disk full?)"); }
static void rd(FILE *f, void *p, size_t n) { if (n && fread(p, 1, n, f) != n) throw runtime_error("truncated or damaged file"); }
template <class T> static void put(FILE *f, T v) { wr(f, &v, sizeof v); }
template <class T> static T get(FILE *f) { T v; rd(f, &v, sizeof v); return v; }
static void copy_n(FILE *from, FILE *to, uint64_t n) { vector<uint8_t> b(1 << 20); while (n) { size_t k = (size_t)min<uint64_t>(n, b.size()); rd(from, b.data(), k); wr(to, b.data(), k); n -= k; } }
static uint64_t fsize(const string &p) { error_code ec; auto s = fs::file_size(p, ec); if (ec) throw runtime_error("missing file " + p); return s; }

// ---- labelling (same rules as hs_wrap.py) ----
static void features(const uint8_t *c, size_t L, array<double, 7> &F, double &e) {
    double h[256] = {0}; for (size_t i = 0; i < L; i++) h[c[i]]++;
    e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double p = h[i] / L; e -= p * log2(p); }
    double alpha = 0, digit = 0, high = 0, ctl = 0;
    for (int i = 65; i < 91; i++) alpha += h[i]; for (int i = 97; i < 123; i++) alpha += h[i];
    for (int i = 48; i < 58; i++) digit += h[i]; for (int i = 128; i < 256; i++) high += h[i];
    for (int i = 0; i < 32; i++) ctl += h[i]; ctl += h[127] - h[10] - h[13] - h[9];
    double space = h[32] + h[10] + h[13] + h[9];
    F = {ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8};
}
static vector<int> labels(const vector<array<double, 7>> &F, const vector<double> &E, const vector<size_t> &Ls) {
    size_t nw = F.size(); vector<int> lab(nw, 0); vector<size_t> idx;
    for (size_t i = 0; i < nw; i++) { if (E[i] > 7.99 && Ls[i] >= 4096) lab[i] = RAW; else idx.push_back(i); }
    int K = idx.size() >= 48 ? 3 : (idx.size() >= 16 ? 2 : 1);
    if (!idx.empty()) {
        size_t m = idx.size(); auto d2 = [&](const array<double, 7> &a, const array<double, 7> &b) { double s = 0; for (int k = 0; k < 7; k++) s += (a[k] - b[k]) * (a[k] - b[k]); return s; };
        vector<array<double, 7>> C{F[idx[0]]};
        for (int k = 1; k < K; k++) {
            size_t best = 0; double bd = -1;
            for (size_t j = 0; j < m; j++) { double d = 1e300; for (auto &c : C) d = min(d, d2(F[idx[j]], c)); if (d > bd) { bd = d; best = j; } }
            C.push_back(F[idx[best]]);
        }
        vector<int> a(m);
        for (int it = 0; it < 20; it++) {
            for (size_t j = 0; j < m; j++) { int b = 0; double bd = d2(F[idx[j]], C[0]); for (int k = 1; k < K; k++) { double d = d2(F[idx[j]], C[k]); if (d < bd) { bd = d; b = k; } } a[j] = b; }
            for (int k = 0; k < K; k++) {
                array<double, 7> s{}; size_t c = 0;
                for (size_t j = 0; j < m; j++) if (a[j] == k) { for (int q = 0; q < 7; q++) s[q] += F[idx[j]][q]; c++; }
                if (c) for (int q = 0; q < 7; q++) C[k][q] = s[q] / c;
            }
        }
        vector<size_t> cnt(K, 0); for (int x : a) cnt[x]++;
        int big = (int)(max_element(cnt.begin(), cnt.end()) - cnt.begin());
        for (size_t j = 0; j < m; j++) lab[idx[j]] = cnt[a[j]] * W < (1u << 20) ? big : a[j];
    }
    for (int pass = 0; pass < 2; pass++) {
        size_t i = 0;
        while (i < nw) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; if (j - i < 8 && i > 0) for (size_t q = i; q < j; q++) lab[q] = lab[i - 1]; i = j; }
    }
    return lab;
}

// zlib's crc32_combine: CRC of A+B from CRC(A), CRC(B) and len(B)
static uint32_t gf2_times(const uint32_t *m, uint32_t v) { uint32_t s = 0; while (v) { if (v & 1) s ^= *m; v >>= 1; m++; } return s; }
static void gf2_square(uint32_t *sq, const uint32_t *m) { for (int n = 0; n < 32; n++) sq[n] = gf2_times(m, m[n]); }
static uint32_t crc_combine(uint32_t c1, uint32_t c2, uint64_t len2) {
    if (!len2) return c1; uint32_t even[32], odd[32]; odd[0] = 0xEDB88320u; uint32_t row = 1; for (int n = 1; n < 32; n++) { odd[n] = row; row <<= 1; }
    gf2_square(even, odd); gf2_square(odd, even);
    do { gf2_square(even, odd); if (len2 & 1) c1 = gf2_times(even, c1); len2 >>= 1; if (!len2) break; gf2_square(odd, even); if (len2 & 1) c1 = gf2_times(odd, c1); len2 >>= 1; } while (len2);
    return c1 ^ c2;
}

// ---- output file mapped into memory ----
struct OutMap {
    uint8_t *p = nullptr; uint64_t n = 0;
#ifdef _WIN32
    HANDLE f = INVALID_HANDLE_VALUE, m = nullptr;
    OutMap(const string &path, uint64_t size) : n(size) {
        f = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) throw runtime_error("cannot create " + path);
        if (!n) return;
        m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, (DWORD)(n >> 32), (DWORD)(n & 0xffffffffu), nullptr); if (!m) throw runtime_error("cannot map output (disk full?)");
        p = (uint8_t *)MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, (SIZE_T)n); if (!p) throw runtime_error("cannot map output view");
    }
    ~OutMap() { if (p) UnmapViewOfFile(p); if (m) CloseHandle(m); if (f != INVALID_HANDLE_VALUE) CloseHandle(f); }
#else
    int fd = -1;
    OutMap(const string &path, uint64_t size) : n(size) {
        fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644); if (fd < 0) throw runtime_error("cannot create " + path);
        if (!n) return;
        if (ftruncate(fd, (off_t)n)) throw runtime_error("cannot size output (disk full?)");
        void *q = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); if (q == MAP_FAILED) throw runtime_error("cannot map output"); p = (uint8_t *)q;
    }
    ~OutMap() { if (p) munmap(p, n); if (fd >= 0) close(fd); }
#endif
};
static void pread_at(FILE *f, uint64_t off, void *dst, size_t n) { if (FSEEK(f, (long long)off, SEEK_SET)) throw runtime_error("seek failed (truncated or damaged file)"); rd(f, dst, n); }
struct CCtxPtr { ZSTD_CCtx *p = ZSTD_createCCtx(); ~CCtxPtr() { ZSTD_freeCCtx(p); } };   // freed on every exit path
struct DCtxPtr { ZSTD_DCtx *p = ZSTD_createDCtx(); ~DCtxPtr() { ZSTD_freeDCtx(p); } };

static void compress(const string &in, const string &out, int level, int jobs, long block_mb) {
    uint64_t n = fsize(in), nw = (n + W - 1) / W;
    // pass 1 (all cores): window features + CRC per range, CRCs joined
    vector<array<double, 7>> Fv(nw); vector<double> E(nw); vector<size_t> Ls(nw);
    int T = max(1, jobs); vector<uint32_t> rc(T, 0); vector<uint64_t> rlen(T, 0); string err; mutex mu;
    { vector<thread> th; for (int t = 0; t < T; t++) th.emplace_back([&, t] {
        try { uint64_t a = nw * t / T, b = nw * (t + 1) / T; F fi(in, "rb"); FSEEK(fi.f, (long long)(a * W), SEEK_SET); vector<uint8_t> buf(W); uint32_t c = 0; uint64_t L = 0;
            for (uint64_t i = a; i < b; i++) { size_t k = (size_t)min<uint64_t>(W, n - i * W); rd(fi.f, buf.data(), k); features(buf.data(), k, Fv[i], E[i]); Ls[i] = k; c = crc_upd(c, buf.data(), k); L += k; }
            rc[t] = c; rlen[t] = L; } catch (exception &e) { lock_guard<mutex> g(mu); err = e.what(); } });
      for (auto &x : th) x.join(); }
    if (!err.empty()) throw runtime_error(err);
    uint32_t crc = 0; for (int t = 0; t < T; t++) crc = crc_combine(crc, rc[t], rlen[t]);
    auto lab = labels(Fv, E, Ls);
    vector<pair<int, uint32_t>> R; for (size_t i = 0; i < nw;) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; R.push_back({lab[i], (uint32_t)(j - i)}); i = j; }
    map<int, uint64_t> setlen; for (size_t i = 0; i < nw; i++) setlen[lab[i]] += Ls[i];
    // blocks per set (same split as hs_wrap), and for each block the input pieces it is made of
    struct Blk { int s; uint64_t len; vector<pair<uint64_t, uint64_t>> pieces; }; vector<Blk> B; map<int, uint64_t> per; map<int, size_t> first;
    for (auto &kv : setlen) { int s = kv.first; if (s == RAW) continue; uint64_t L = kv.second, BB = (uint64_t)block_mb << 20, nb = max<uint64_t>(1, (L + BB - 1) / BB), p = (L + nb - 1) / nb;
        per[s] = p; first[s] = B.size(); for (uint64_t k = 0; k < nb; k++) B.push_back({s, min(L, (k + 1) * p) - min(L, k * p), {}}); }
    vector<pair<uint64_t, uint64_t>> rawp; map<int, uint64_t> pos;
    for (size_t i = 0; i < nw; i++) { int s = lab[i]; uint64_t off = (uint64_t)i * W, k = Ls[i];
        if (s == RAW) { if (!rawp.empty() && rawp.back().first + rawp.back().second == off) rawp.back().second += k; else rawp.push_back({off, k}); continue; }
        while (k) { size_t bk = first[s] + (size_t)(pos[s] / per[s]); uint64_t room = (bk - first[s] + 1) * per[s] - pos[s], t = min(room, k); auto &pc = B[bk].pieces;
            if (!pc.empty() && pc.back().first + pc.back().second == off) pc.back().second += t; else pc.push_back({off, t}); off += t; k -= t; pos[s] += t; } }
    size_t nb = B.size(); vector<vector<uint8_t>> z(nb); vector<char> done(nb, 0); size_t held = 0, LIMIT = (size_t)T * 2 + 2; atomic<size_t> nx{0}; condition_variable cv;
    F fo(out, "wb"); setvbuf(fo.f, nullptr, _IOFBF, 1 << 22);
    wr(fo.f, "HSZ1", 4); put<uint64_t>(fo.f, n); put<uint32_t>(fo.f, crc); put<uint32_t>(fo.f, (uint32_t)R.size()); put<uint32_t>(fo.f, (uint32_t)nb);
    for (auto &r : R) { put<uint8_t>(fo.f, (uint8_t)r.first); put<uint32_t>(fo.f, r.second); }
    long long tablepos = (long long)(4 + 20 + 5 * R.size());
    for (auto &b : B) { put<uint8_t>(fo.f, (uint8_t)b.s); put<uint64_t>(fo.f, b.len); put<uint64_t>(fo.f, 0); }
    uint64_t rl = setlen.count(RAW) ? setlen[RAW] : 0; put<uint64_t>(fo.f, rl);
    vector<thread> th; for (int t = 0; t < T; t++) th.emplace_back([&] {
        try { F fi(in, "rb"); CCtxPtr ccp; ZSTD_CCtx *cc = ccp.p; vector<uint8_t> buf;
            for (;;) { size_t k;
                { unique_lock<mutex> g(mu); cv.wait(g, [&] { return held < LIMIT || !err.empty(); }); if (!err.empty()) break; k = nx++; if (k >= nb) break; held++; }
                buf.resize(B[k].len); size_t o = 0; for (auto &pc : B[k].pieces) { pread_at(fi.f, pc.first, buf.data() + o, (size_t)pc.second); o += pc.second; }
                vector<uint8_t> zz(ZSTD_compressBound(buf.size())); size_t r = ZSTD_compressCCtx(cc, zz.data(), zz.size(), buf.data(), buf.size(), level);
                if (ZSTD_isError(r)) throw runtime_error(string("zstd: ") + ZSTD_getErrorName(r)); zz.resize(r);
                lock_guard<mutex> g(mu); z[k].swap(zz); done[k] = 1; cv.notify_all(); }
            } catch (exception &e) { lock_guard<mutex> g(mu); if (err.empty()) err = e.what(); cv.notify_all(); } });
    try {   // writer: raw windows first, then the blocks in order as they finish
        { F fi(in, "rb"); vector<uint8_t> b(1 << 22); for (auto &rp : rawp) { uint64_t o = rp.first, L = rp.second; while (L) { size_t t = (size_t)min<uint64_t>(L, b.size()); pread_at(fi.f, o, b.data(), t); wr(fo.f, b.data(), t); o += t; L -= t; } } }
        for (size_t k = 0; k < nb; k++) { vector<uint8_t> zz;
            { unique_lock<mutex> g(mu); cv.wait(g, [&] { return done[k] || !err.empty(); }); if (!done[k]) throw runtime_error(err); zz.swap(z[k]); held--; cv.notify_all(); }
            B[k].len = B[k].len; wr(fo.f, zz.data(), zz.size()); z[k].clear(); B[k].pieces.clear(); B[k].pieces.push_back({0, zz.size()}); }
    } catch (exception &e) { lock_guard<mutex> g(mu); if (err.empty()) err = e.what(); cv.notify_all(); }
    for (auto &x : th) x.join(); if (!err.empty()) throw runtime_error(err);
    FSEEK(fo.f, tablepos, SEEK_SET); for (auto &b : B) { put<uint8_t>(fo.f, (uint8_t)b.s); put<uint64_t>(fo.f, b.len); put<uint64_t>(fo.f, b.pieces[0].second); }
    uint64_t zt = 0; for (auto &b : B) zt += b.pieces[0].second; uint64_t outsz = (uint64_t)tablepos + 17ull * nb + 8 + rl + zt;
    printf("%llu -> %llu bytes; runs %zu, blocks %zu, raw %llu, zstd level %d\n", (unsigned long long)n, (unsigned long long)outsz, R.size(), nb, (unsigned long long)rl, level);
}

static void decompress(const string &in, const string &out, int jobs) {
    uint64_t total = fsize(in); F fi(in, "rb"); char mg[4]; rd(fi.f, mg, 4); if (memcmp(mg, "HSZ1", 4)) throw runtime_error("not an hs_zstd file");
    uint64_t n = get<uint64_t>(fi.f); uint32_t crc = get<uint32_t>(fi.f), nr = get<uint32_t>(fi.f), nbk = get<uint32_t>(fi.f);
    if (32 + 5ull * nr + 17ull * nbk > total) throw runtime_error("truncated or damaged file");   // 1.2: full header size checked first
    if (n > (1ull << 60)) throw runtime_error("damaged file (impossible length)");
    vector<pair<int, uint32_t>> R(nr); for (auto &r : R) { r.first = get<uint8_t>(fi.f); r.second = get<uint32_t>(fi.f); }
    struct Bk { int s; uint64_t ln, zl; }; vector<Bk> B(nbk); for (auto &b : B) { b.s = get<uint8_t>(fi.f); b.ln = get<uint64_t>(fi.f); b.zl = get<uint64_t>(fi.f); }
    uint64_t rl = get<uint64_t>(fi.f), p = 4 + 20 + 5ull * nr + 17ull * nbk + 8; if (rl > total - p) throw runtime_error("bad raw length");
    uint64_t rawpos = p; p += rl; uint64_t nw = (n + W - 1) / W, ztot = 0;
    for (auto &b : B) { if (b.zl > total - p || b.ln > n || b.ln == 0) throw runtime_error("block table is damaged"); ztot += b.zl; if (ztot > total - p) throw runtime_error("wrong file length (truncated or damaged)"); }
    if (p + ztot != total) throw runtime_error("wrong file length (truncated or damaged)");
    size_t nb = B.size(); vector<uint64_t> zoff(nb); { uint64_t q = p; for (size_t k = 0; k < nb; k++) { zoff[k] = q; q += B[k].zl; } }
    // pieces of the output in order: block (or raw) + offset inside it + length + where it goes
    struct Seg { int64_t blk; uint64_t off, len, out; uint32_t crc; }; vector<Seg> seg; map<int, vector<size_t>> bl; for (size_t k = 0; k < nb; k++) bl[B[k].s].push_back(k);
    map<int, pair<size_t, uint64_t>> cur; vector<uint64_t> firstneed(nb, UINT64_MAX); vector<vector<size_t>> segs_of(nb); uint64_t outpos = 0, rawoff = 0, wi = 0;
    for (auto &r : R) {
        if (r.first != RAW && r.first > 2) throw runtime_error("bad set label in the map"); if (r.second == 0 || wi + r.second > nw) throw runtime_error("the map does not match the file length");
        uint64_t L = min<uint64_t>((uint64_t)r.second * W, n - wi * W); wi += r.second;
        if (r.first == RAW) { if (rawoff + L > rl) throw runtime_error("bad raw length"); seg.push_back({-1, rawoff, L, outpos, 0}); rawoff += L; outpos += L; continue; }
        auto &c = cur[r.first]; auto &lst = bl[r.first];
        while (L) { if (c.first >= lst.size()) throw runtime_error("set data ran out"); size_t k = lst[c.first]; uint64_t t = min<uint64_t>(L, B[k].ln - c.second);
            if (firstneed[k] == UINT64_MAX) firstneed[k] = outpos; segs_of[k].push_back(seg.size()); seg.push_back({(int64_t)k, c.second, t, outpos, 0});
            c.second += t; L -= t; outpos += t; if (c.second == B[k].ln) { c.first++; c.second = 0; } }
    }
    if (wi != nw || outpos != n || rawoff != rl) throw runtime_error("the map does not match the file length");
    for (auto &kv : cur) { auto &lst = bl[kv.first]; if (kv.second.first < lst.size()) throw runtime_error("block table does not match the map"); }
    // tasks: every block, plus the raw pieces (one task per raw piece), in the order the output needs them
    struct Task { int64_t blk; size_t si; uint64_t key; }; vector<Task> tasks;
    for (size_t k = 0; k < nb; k++) tasks.push_back({(int64_t)k, 0, firstneed[k] == UINT64_MAX ? n : firstneed[k]});
    for (size_t i = 0; i < seg.size(); i++) if (seg[i].blk < 0) tasks.push_back({-1, i, seg[i].out});
    stable_sort(tasks.begin(), tasks.end(), [](const Task &a, const Task &b) { return a.key < b.key; });
    OutMap om(out, n); atomic<size_t> nx{0}; mutex mu; string err;
    vector<thread> th; for (int t = 0; t < max(1, jobs); t++) th.emplace_back([&] {
        try { F f2(in, "rb"); DCtxPtr dcp; ZSTD_DCtx *dc = dcp.p; vector<uint8_t> zb;
            for (size_t j; (j = nx++) < tasks.size();) {
                { lock_guard<mutex> g(mu); if (!err.empty()) break; }
                auto &tk = tasks[j];
                if (tk.blk < 0) { Seg &s = seg[tk.si]; pread_at(f2.f, rawpos + s.off, om.p + s.out, (size_t)s.len); s.crc = crc_upd(0, om.p + s.out, (size_t)s.len); continue; }
                size_t k = (size_t)tk.blk; zb.resize((size_t)B[k].zl); pread_at(f2.f, zoff[k], zb.data(), zb.size());
                if (segs_of[k].size() == 1) {   // 1.1: block lands in one piece of the output -> decode straight into place, no stream buffer
                    Seg &s = seg[segs_of[k][0]]; size_t r = ZSTD_decompressDCtx(dc, om.p + s.out, (size_t)s.len, zb.data(), zb.size());
                    if (ZSTD_isError(r)) throw runtime_error(string("zstd: ") + ZSTD_getErrorName(r)); if (r != s.len) throw runtime_error("block length mismatch");
                    s.crc = crc_upd(0, om.p + s.out, (size_t)s.len); continue; }
                ZSTD_DCtx_reset(dc, ZSTD_reset_session_only); ZSTD_inBuffer ib{zb.data(), zb.size(), 0}; size_t ret = 1;
                for (size_t si : segs_of[k]) { Seg &s = seg[si]; ZSTD_outBuffer ob{om.p + s.out, (size_t)s.len, 0};
                    while (ob.pos < ob.size) { size_t ip0 = ib.pos, op0 = ob.pos; ret = ZSTD_decompressStream(dc, &ob, &ib); if (ZSTD_isError(ret)) throw runtime_error(string("zstd: ") + ZSTD_getErrorName(ret)); if (ret == 0 && ob.pos < ob.size) throw runtime_error("block decoded shorter than expected");
                        if (ib.pos == ip0 && ob.pos == op0) throw runtime_error("damaged block (zstd made no progress: truncated or corrupt frame)"); }   // 1.2: no-progress guard
                    s.crc = crc_upd(0, om.p + s.out, (size_t)s.len); }
                if (ret != 0 || ib.pos != ib.size) throw runtime_error("block length mismatch");
            }
            } catch (exception &e) { lock_guard<mutex> g(mu); if (err.empty()) err = e.what(); } });
    for (auto &x : th) x.join();
    if (!err.empty()) throw runtime_error(err);
    uint32_t c2 = 0; for (auto &s : seg) c2 = crc_combine(c2, s.crc, s.len);
    if (c2 != crc) throw runtime_error("restored data failed the CRC-32 check");
}

int main(int argc, char **argv) {
    crc_init();
    if (argc < 4 || (strcmp(argv[1], "c") && strcmp(argv[1], "d"))) {
        fprintf(stderr, "Harrington Sets + zstd (hs_zstd 1.2), Michael Hugh Harrington, MIT License; zstd %s built in\n"
                        "  hs_zstd c IN OUT [--level L] [--jobs N] [--block-mb B]\n  hs_zstd d IN OUT [--jobs N]\n", ZSTD_versionString());
        return 2;
    }
    int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; int level = 3; long bmb = 16;
    for (int i = 4; i + 1 < argc; i += 2) { string k = argv[i];
        if (k == "--jobs") jobs = atoi(argv[i + 1]); else if (k == "--level") level = atoi(argv[i + 1]); else if (k == "--block-mb") bmb = atol(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; } }
    if (bmb < 1) bmb = 16;
    auto t = chrono::steady_clock::now();
    try { if (argv[1][0] == 'c') compress(argv[2], argv[3], level, jobs, bmb); else decompress(argv[2], argv[3], jobs); }
    catch (exception &e) { fprintf(stderr, "error: %s\n", e.what()); error_code ec; if (argv[1][0] == 'd') fs::remove(argv[3], ec); return 1; }
    printf("%s wall time %.2f s\n", argv[1][0] == 'c' ? "compress" : "decompress", chrono::duration<double>(chrono::steady_clock::now() - t).count());
    return 0;
}

// MIT License
//
// Copyright (c) 2026 Michael Hugh Harrington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

============================== LICENSE ==============================
MIT License

Copyright (c) 2026 Michael Hugh Harrington

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
