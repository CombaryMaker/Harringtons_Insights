// hs_wrap.cpp - Harrington Sets wrapper, C++ port of hs_wrap.py 1.2 (same HSW2 file format; files are
// interchangeable with the Python version). Runs any single-core compressor through Harrington Sets:
// 64 KB windows, entropy exclusion above 7.99 bits/byte, k-means into at most 3 sets, run smoothing,
// blocks of at most B MB, one compressor copy per block on separate cores, run-length map, CRC-32.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Usage:
//   hs_wrap c IN OUT --comp "COMPRESSOR {in} {out}" [--jobs N] [--block-mb B]
//   hs_wrap d IN OUT --decomp "DECOMPRESSOR {in} {out}" [--jobs N]
// {in}/{out} become temporary block files (kept in a folder next to OUT, removed afterwards); a template
// without {out} is read from standard output.
// Build: g++ -O2 -std=c++17 -pthread hs_wrap.cpp -o hs_wrap   (Windows: zig c++ -target x86_64-windows-gnu)
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
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
namespace fs = std::filesystem;
using namespace std;
static const size_t W = 65536; static const int RAW = 255;

static uint32_t crc_tab[256];
static void crc_init() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[i] = c; } }
static uint32_t crc32(const uint8_t *p, size_t n) { uint32_t c = 0xFFFFFFFFu; while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8); return c ^ 0xFFFFFFFFu; }

static vector<uint8_t> readf(const string &f) {
    FILE *fp = fopen(f.c_str(), "rb"); if (!fp) throw runtime_error("cannot open " + f);
    vector<uint8_t> d; uint8_t buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(fp); return d;
}
static void writef(const string &f, const uint8_t *p, size_t n) {
    FILE *fp = fopen(f.c_str(), "wb"); if (!fp) throw runtime_error("cannot write " + f);
    if (n && fwrite(p, 1, n, fp) != n) { fclose(fp); throw runtime_error("write failed: " + f); }
    fclose(fp);
}

// ---- labelling (same rules as hs_wrap.py) ----
static void features(const uint8_t *c, size_t L, double F[7], double &e) {
    double h[256] = {0}; for (size_t i = 0; i < L; i++) h[c[i]]++;
    e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double p = h[i] / L; e -= p * log2(p); }
    double alpha = 0, digit = 0, high = 0, ctl = 0;
    for (int i = 65; i < 91; i++) alpha += h[i]; for (int i = 97; i < 123; i++) alpha += h[i];
    for (int i = 48; i < 58; i++) digit += h[i]; for (int i = 128; i < 256; i++) high += h[i];
    for (int i = 0; i < 32; i++) ctl += h[i]; ctl += h[127] - h[10] - h[13] - h[9];
    double space = h[32] + h[10] + h[13] + h[9];
    F[0] = ctl / L; F[1] = alpha / L; F[2] = digit / L; F[3] = space / L; F[4] = high / L; F[5] = h[0] / L; F[6] = e / 8;
}
static vector<int> labels(const vector<uint8_t> &data) {
    size_t n = data.size(), nw = (n + W - 1) / W; vector<array<double, 7>> F(nw); vector<int> lab(nw, 0); vector<size_t> idx;
    for (size_t i = 0; i < nw; i++) {
        size_t L = min(W, n - i * W); double e; features(&data[i * W], L, F[i].data(), e);
        if (e > 7.99 && L >= 4096) lab[i] = RAW; else idx.push_back(i);
    }
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

// ---- running the compressor ----
static string quote(const string &s) { return "\"" + s + "\""; }
static void call(const string &tmpl, const string &src, const string &dst) {
    string cmd = tmpl; bool hasout = tmpl.find("{out}") != string::npos;
    for (size_t p; (p = cmd.find("{in}")) != string::npos;) cmd.replace(p, 4, quote(src));
    for (size_t p; (p = cmd.find("{out}")) != string::npos;) cmd.replace(p, 5, quote(dst));
#ifdef _WIN32
    cmd += hasout ? " >NUL 2>NUL" : " >" + quote(dst) + " 2>NUL";
    cmd = "\"" + cmd + "\"";   // cmd /c strips one outer pair of quotes
#else
    cmd += hasout ? " >/dev/null 2>&1" : " >" + quote(dst) + " 2>/dev/null";
#endif
    int r = system(cmd.c_str());
    if (r != 0) throw runtime_error("compressor failed (exit " + to_string(r) + "): " + tmpl);
}
static void run_all(const string &tmpl, const vector<pair<string, string>> &pairs, const vector<size_t> &sizes, int jobs) {
    vector<size_t> order(pairs.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
    stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sizes[a] > sizes[b]; });
    atomic<size_t> next{0}; mutex mu; string err;
    auto worker = [&] { for (size_t k; (k = next++) < order.size();) { try { call(tmpl, pairs[order[k]].first, pairs[order[k]].second); } catch (exception &e) { lock_guard<mutex> g(mu); if (err.empty()) err = e.what(); } } };
    vector<thread> th; for (int i = 0; i < max(1, jobs); i++) th.emplace_back(worker); for (auto &t : th) t.join();
    if (!err.empty()) throw runtime_error(err);
}
template <class T> static void put(vector<uint8_t> &o, T v) { uint8_t b[sizeof(T)]; memcpy(b, &v, sizeof(T)); o.insert(o.end(), b, b + sizeof(T)); }
template <class T> static T get(const vector<uint8_t> &d, size_t &p) { if (p + sizeof(T) > d.size()) throw runtime_error("truncated or damaged file"); T v; memcpy(&v, &d[p], sizeof(T)); p += sizeof(T); return v; }

struct TmpDir { string path; TmpDir(const string &out) { path = out + ".hsw_tmp"; fs::remove_all(path); fs::create_directories(path); } ~TmpDir() { error_code ec; fs::remove_all(path, ec); } };

static void compress(const string &in, const string &out, const string &comp, int jobs, long block_mb) {
    auto data = readf(in); auto lab = labels(data); size_t nw = lab.size(), n = data.size();
    vector<pair<int, uint32_t>> R; for (size_t i = 0; i < nw;) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; R.push_back({lab[i], (uint32_t)(j - i)}); i = j; }
    set<int> S(lab.begin(), lab.end()); vector<pair<int, vector<uint8_t>>> blocks; vector<uint8_t> raw;
    for (int s : S) {
        vector<uint8_t> buf; for (size_t i = 0; i < nw; i++) if (lab[i] == s) buf.insert(buf.end(), data.begin() + i * W, data.begin() + min(n, (i + 1) * W));
        if (s == RAW) { raw = move(buf); continue; }
        size_t B = (size_t)block_mb << 20, nb = max<size_t>(1, (buf.size() + B - 1) / B), per = (buf.size() + nb - 1) / nb;
        for (size_t k = 0; k < nb; k++) { size_t a = min(buf.size(), k * per), b = min(buf.size(), (k + 1) * per); blocks.push_back({s, vector<uint8_t>(buf.begin() + a, buf.begin() + b)}); }
    }
    vector<vector<uint8_t>> zs;
    {
        TmpDir td(out); vector<pair<string, string>> pairs; vector<size_t> sizes;
        for (size_t k = 0; k < blocks.size(); k++) { string f = (fs::path(td.path) / ("b" + to_string(k))).string(); writef(f, blocks[k].second.data(), blocks[k].second.size()); pairs.push_back({f, f + ".z"}); sizes.push_back(blocks[k].second.size()); }
        run_all(comp, pairs, sizes, jobs);
        for (auto &p : pairs) zs.push_back(readf(p.second));
    }
    vector<uint8_t> o = {'H', 'S', 'W', '2'};
    put<uint64_t>(o, n); put<uint32_t>(o, crc32(data.data(), n)); put<uint32_t>(o, (uint32_t)R.size()); put<uint32_t>(o, (uint32_t)blocks.size());
    for (auto &r : R) { put<uint8_t>(o, (uint8_t)r.first); put<uint32_t>(o, r.second); }
    for (size_t k = 0; k < blocks.size(); k++) { put<uint8_t>(o, (uint8_t)blocks[k].first); put<uint64_t>(o, blocks[k].second.size()); put<uint64_t>(o, zs[k].size()); }
    put<uint64_t>(o, raw.size()); o.insert(o.end(), raw.begin(), raw.end());
    for (auto &z : zs) o.insert(o.end(), z.begin(), z.end());
    writef(out, o.data(), o.size());
    printf("%zu -> %zu bytes; sets", n, o.size()); for (int s : S) printf(" %d", s); printf(", runs %zu, blocks %zu, raw %zu\n", R.size(), blocks.size(), raw.size());
}

static void decompress(const string &in, const string &out, const string &decomp, int jobs) {
    auto d = readf(in); size_t p = 4; uint64_t n; uint32_t crc = 0, nr, nbk; bool hascrc;
    if (d.size() >= 4 && !memcmp(d.data(), "HSW2", 4)) { n = get<uint64_t>(d, p); crc = get<uint32_t>(d, p); nr = get<uint32_t>(d, p); nbk = get<uint32_t>(d, p); hascrc = true; }
    else if (d.size() >= 4 && !memcmp(d.data(), "HSW1", 4)) { n = get<uint64_t>(d, p); nr = get<uint32_t>(d, p); nbk = get<uint32_t>(d, p); hascrc = false; }
    else throw runtime_error("not a Harrington Sets wrapper file");
    if ((uint64_t)nr * 5 + (uint64_t)nbk * 17 > d.size()) throw runtime_error("truncated or damaged file");
    vector<pair<int, uint32_t>> R(nr); for (auto &r : R) { r.first = get<uint8_t>(d, p); r.second = get<uint32_t>(d, p); }
    struct Bk { int s; uint64_t ln, zl; }; vector<Bk> B(nbk); for (auto &b : B) { b.s = get<uint8_t>(d, p); b.ln = get<uint64_t>(d, p); b.zl = get<uint64_t>(d, p); }
    uint64_t rl = get<uint64_t>(d, p); if (rl > d.size() - p) throw runtime_error("bad raw length");
    vector<uint8_t> raw(d.begin() + p, d.begin() + p + rl); p += rl;
    // validate the container against the map before running any decoder
    uint64_t nw = (n + W - 1) / W, i = 0; map<int, uint64_t> need;
    for (auto &r : R) {
        if (r.first != RAW && r.first > 2) throw runtime_error("bad set label in the map");
        if (r.second == 0 || i + r.second > nw) throw runtime_error("the map does not match the file length");
        need[r.first] += (uint64_t)r.second * W - (i + r.second == nw ? nw * W - n : 0); i += r.second;
    }
    if (i != nw) throw runtime_error("the map does not match the file length");
    if (rl != (need.count(RAW) ? need[RAW] : 0)) throw runtime_error("bad raw length");
    map<int, uint64_t> got, want; uint64_t ztot = 0;
    for (auto &b : B) { if (b.s == RAW) throw runtime_error("block table does not match the map"); got[b.s] += b.ln; ztot += b.zl; }
    for (auto &kv : need) if (kv.first != RAW) want[kv.first] = kv.second;
    if (got != want) throw runtime_error("block table does not match the map");
    if (p + ztot != d.size()) throw runtime_error("wrong file length (truncated or damaged)");
    map<int, vector<uint8_t>> sb;
    {
        TmpDir td(out); vector<pair<string, string>> pairs; vector<size_t> sizes;
        for (size_t k = 0; k < B.size(); k++) { string f = (fs::path(td.path) / ("b" + to_string(k))).string(); writef(f + ".z", &d[p], B[k].zl); p += B[k].zl; pairs.push_back({f + ".z", f}); sizes.push_back(B[k].ln); }
        run_all(decomp, pairs, sizes, jobs);
        for (size_t k = 0; k < B.size(); k++) {
            auto b = readf(pairs[k].second);
            if (b.size() != B[k].ln) throw runtime_error("block length mismatch");
            sb[B[k].s].insert(sb[B[k].s].end(), b.begin(), b.end());
        }
    }
    if (!raw.empty()) sb[RAW] = move(raw);
    map<int, size_t> pos; vector<uint8_t> o; o.reserve(n); i = 0;
    for (auto &r : R) for (uint32_t c = 0; c < r.second; c++, i++) { size_t L = (size_t)min<uint64_t>(W, n - i * W); auto &v = sb[r.first]; o.insert(o.end(), v.begin() + pos[r.first], v.begin() + pos[r.first] + L); pos[r.first] += L; }
    if (o.size() != n || (hascrc && crc32(o.data(), o.size()) != crc)) throw runtime_error("restored data failed the CRC-32 check");
    writef(out, o.data(), o.size());
}

int main(int argc, char **argv) {
    crc_init();
    if (argc < 4 || (strcmp(argv[1], "c") && strcmp(argv[1], "d"))) {
        fprintf(stderr, "Harrington Sets wrapper (C++ port of hs_wrap.py 1.2), Michael Hugh Harrington, MIT License\n"
                        "  hs_wrap c IN OUT --comp \"COMPRESSOR {in} {out}\" [--jobs N] [--block-mb B]\n"
                        "  hs_wrap d IN OUT --decomp \"DECOMPRESSOR {in} {out}\" [--jobs N]\n");
        return 2;
    }
    string comp, decomp; int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; long bmb = 16;
    for (int i = 4; i + 1 < argc; i += 2) {
        string k = argv[i];
        if (k == "--comp") comp = argv[i + 1]; else if (k == "--decomp") decomp = argv[i + 1];
        else if (k == "--jobs") jobs = atoi(argv[i + 1]); else if (k == "--block-mb") bmb = atol(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    bool c = argv[1][0] == 'c';
    if (c ? comp.empty() : decomp.empty()) { fprintf(stderr, "error: --comp is needed to compress, --decomp to decompress\n"); return 2; }
    if (bmb < 1) bmb = 16;
    auto t = chrono::steady_clock::now();
    try { c ? compress(argv[2], argv[3], comp, jobs, bmb) : decompress(argv[2], argv[3], decomp, jobs); }
    catch (exception &e) { fprintf(stderr, "error: %s\n", e.what()); return 1; }
    printf("%s wall time %.1f s\n", c ? "compress" : "decompress", chrono::duration<double>(chrono::steady_clock::now() - t).count());
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
