// hs_lzav.cpp - Harrington's Sets + LZAV: one large buffer compressed as many independent, STANDARD LZAV
// streams that decode in parallel on all cores, each core writing straight into its final place in the output.
//
// Michael Hugh Harrington, 2026. MIT License (see LICENSE.txt in this folder).
// LZAV by Aleksey Vaneev (MIT) is included unmodified (lzav.h); every block is a plain LZAV format-4 stream that
// the stock lzav_decompress() decodes. lzav_hi.h (this project) is an optional stronger encoder for the blocks.
//
// Method (public, as in hs_zstd 1.3): the input is labelled in 64 KB windows by 7 byte statistics (shares of
// control bytes, letters, digits, spaces, high bytes, zeros, and entropy), grouped by k-means into at most 3 sets,
// short runs smoothed; each set's windows in file order form one stream, which is cut into blocks of B bytes;
// every block is compressed independently. --sets 0 keeps file order (plain block splitting, the control).
// A block whose output is one contiguous piece of the file is decoded straight into the output; a block that
// covers several non-adjacent windows is decoded into a per-thread buffer and copied out (the cost of Sets).
//
// This file is an in-memory benchmark/library; it does not define a stable on-disk container.
//
// Usage: hs_lzav FILE [--block-mb B] [--sets K] [--codec lzav|lzavhi|hi2|hi2fast] [--runs R] [--threads 1,2,4]
#include "lzav.h"
#include "lzav_hi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
using namespace std;

static const size_t W = 65536;

// ---- labelling: same features, k-means and smoothing rules as hs_zstd 1.3 (no raw windows) ----
static void features(const uint8_t *c, size_t L, array<double, 7> &F) {
    double h[256] = {0}; for (size_t i = 0; i < L; i++) h[c[i]]++;
    double e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double p = h[i] / L; e -= p * log2(p); }
    double alpha = 0, digit = 0, high = 0, ctl = 0;
    for (int i = 65; i < 91; i++) alpha += h[i]; for (int i = 97; i < 123; i++) alpha += h[i];
    for (int i = 48; i < 58; i++) digit += h[i]; for (int i = 128; i < 256; i++) high += h[i];
    for (int i = 0; i < 32; i++) ctl += h[i]; ctl += h[127] - h[10] - h[13] - h[9];
    double space = h[32] + h[10] + h[13] + h[9];
    F = {ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8};
}
static double d2f(const array<double, 7> &a, const array<double, 7> &b) { double s = 0; for (int k = 0; k < 7; k++) s += (a[k] - b[k]) * (a[k] - b[k]); return s; }
static vector<int> labels(const vector<array<double, 7>> &F, int maxK) {
    size_t nw = F.size(); vector<int> lab(nw, 0);
    int K = nw >= 48 ? 3 : (nw >= 16 ? 2 : 1); K = min(K, max(1, maxK));
    if (K > 1) {
        vector<array<double, 7>> C(1, F[0]);
        for (int k = 1; k < K; k++) {   // farthest-point seeding
            size_t best = 0; double bd = -1;
            for (size_t j = 0; j < nw; j++) { double d = 1e300; for (auto &c : C) d = min(d, d2f(F[j], c)); if (d > bd) { bd = d; best = j; } }
            C.push_back(F[best]);
        }
        vector<int> a(nw, 0);
        for (int it = 0; it < 20; it++) {
            for (size_t j = 0; j < nw; j++) { int b = 0; double bd = d2f(F[j], C[0]); for (int k = 1; k < K; k++) { double d = d2f(F[j], C[k]); if (d < bd) { bd = d; b = k; } } a[j] = b; }
            for (int k = 0; k < K; k++) { array<double, 7> s{}; size_t c = 0;
                for (size_t j = 0; j < nw; j++) if (a[j] == k) { for (int q = 0; q < 7; q++) s[q] += F[j][q]; c++; }
                if (c) for (int q = 0; q < 7; q++) C[k][q] = s[q] / c; }
        }
        vector<size_t> cnt(K, 0); for (int x : a) cnt[x]++;
        int big = (int)(max_element(cnt.begin(), cnt.end()) - cnt.begin());
        for (size_t j = 0; j < nw; j++) lab[j] = cnt[a[j]] * W < (1u << 20) ? big : a[j];   // tiny sets fold into the largest
    }
    for (int pass = 0; pass < 2; pass++) {   // runs shorter than 8 windows take the label of the run before
        size_t i = 0;
        while (i < nw) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++;
            if (j - i < 8 && i > 0) for (size_t q = i; q < j; q++) lab[q] = lab[i - 1];
            i = j; }
    }
    return lab;
}

// ---- container (in memory) ----
struct Piece { uint64_t file_off; uint32_t len; };
struct Block { int set; uint64_t stream_off; uint32_t raw_len; uint32_t comp_len; uint64_t comp_off; };
struct Packed {
    uint64_t n = 0;
    vector<uint8_t> lab;              // set of each 64 KB window (stored as runs; size counted below)
    vector<Block> blocks;
    vector<uint8_t> payload;
    size_t header_bytes() const {     // what a serialised container would need: runs map + block table + 16
        size_t runs = 0; for (size_t i = 0; i < lab.size(); i++) if (i == 0 || lab[i] != lab[i - 1]) runs++;
        return 16 + runs * 5 + blocks.size() * 8;
    }
    size_t total() const { return payload.size() + header_bytes(); }
};

// windows of each set, in file order
static vector<vector<uint64_t>> set_windows(const vector<uint8_t> &lab, int nsets) {
    vector<vector<uint64_t>> s(nsets); for (size_t i = 0; i < lab.size(); i++) s[lab[i]].push_back(i); return s;
}
// pieces of the file covered by stream range [off, off+len) of a set
static void pieces(const vector<uint64_t> &wins, uint64_t n, uint64_t off, uint64_t len, vector<Piece> &out) {
    out.clear();
    while (len) {
        uint64_t wi = off / W, wo = off % W; uint64_t fo = wins[wi] * W + wo;
        uint64_t wl = min<uint64_t>(W, n - wins[wi] * W) - wo; uint64_t take = min(wl, len);
        if (!out.empty() && out.back().file_off + out.back().len == fo) out.back().len += (uint32_t)take;
        else out.push_back({fo, (uint32_t)take});
        off += take; len -= take;
    }
}

static int comp_block(const string &codec, const uint8_t *s, int n, uint8_t *d, int cap) {
    if (codec == "lzav") return lzav_compress_default(s, d, n, cap);
    if (codec == "lzavhi") return lzav_compress_hi(s, d, n, cap);
    if (codec == "hi2") return lzav_compress_hi2(s, d, n, cap, &lzavhi_default_params);
    if (codec == "hi2fast") return lzav_compress_hi2(s, d, n, cap, &lzavhi_fast_params);
    fprintf(stderr, "unknown codec\n"); exit(2);
}

static Packed compress(const uint8_t *src, uint64_t n, size_t B, int maxK, const string &codec, int threads) {
    Packed P; P.n = n; size_t nw = (n + W - 1) / W;
    vector<array<double, 7>> F(nw);
    for (size_t i = 0; i < nw; i++) features(src + i * W, min<uint64_t>(W, n - i * W), F[i]);
    vector<int> lab = maxK >= 2 ? labels(F, maxK) : vector<int>(nw, 0);
    int nsets = 1 + *max_element(lab.begin(), lab.end());
    P.lab.assign(lab.begin(), lab.end());
    auto SW = set_windows(P.lab, nsets);
    for (int k = 0; k < nsets; k++) {
        uint64_t slen = 0; for (auto w : SW[k]) slen += min<uint64_t>(W, n - w * W);
        for (uint64_t o = 0; o < slen; o += B) P.blocks.push_back({k, o, (uint32_t)min<uint64_t>(B, slen - o), 0, 0});
    }
    vector<vector<uint8_t>> out(P.blocks.size());
    atomic<size_t> next{0};
    auto work = [&]() {
        vector<uint8_t> gather; vector<Piece> pc;
        for (size_t b; (b = next++) < P.blocks.size();) {
            Block &bk = P.blocks[b];
            pieces(SW[bk.set], n, bk.stream_off, bk.raw_len, pc);
            const uint8_t *in;
            if (pc.size() == 1) in = src + pc[0].file_off;
            else { gather.resize(bk.raw_len); size_t q = 0; for (auto &p : pc) { memcpy(&gather[q], src + p.file_off, p.len); q += p.len; } in = gather.data(); }
            int cap = lzav_compress_bound_hi((int)bk.raw_len) + lzav_compress_bound((int)bk.raw_len) + 64;
            out[b].resize(cap);
            int cl = comp_block(codec, in, (int)bk.raw_len, out[b].data(), cap);
            if (cl <= 0) { fprintf(stderr, "compress failed\n"); exit(3); }
            out[b].resize(cl); bk.comp_len = (uint32_t)cl;
        }
    };
    vector<thread> th; for (int t = 0; t < threads; t++) th.emplace_back(work); for (auto &t : th) t.join();
    for (size_t b = 0; b < P.blocks.size(); b++) { P.blocks[b].comp_off = P.payload.size(); P.payload.insert(P.payload.end(), out[b].begin(), out[b].end()); }
    return P;
}

// Decoding uses ONLY the stock lzav_decompress().
static bool decompress(const Packed &P, uint8_t *dst, int threads) {
    int nsets = 1 + *max_element(P.lab.begin(), P.lab.end());
    auto SW = set_windows(P.lab, nsets);
    atomic<size_t> next{0}; atomic<bool> ok{true};
    auto work = [&]() {
        vector<uint8_t> tmp; vector<Piece> pc;
        for (size_t b; (b = next++) < P.blocks.size();) {
            const Block &bk = P.blocks[b];
            pieces(SW[bk.set], P.n, bk.stream_off, bk.raw_len, pc);
            const uint8_t *cs = P.payload.data() + bk.comp_off;
            if (pc.size() == 1) {   // straight into the output
                if (lzav_decompress(cs, dst + pc[0].file_off, (int)bk.comp_len, (int)bk.raw_len) != (int)bk.raw_len) ok = false;
            } else {
                tmp.resize(bk.raw_len);
                if (lzav_decompress(cs, tmp.data(), (int)bk.comp_len, (int)bk.raw_len) != (int)bk.raw_len) ok = false;
                size_t q = 0; for (auto &p : pc) { memcpy(dst + p.file_off, &tmp[q], p.len); q += p.len; }
            }
        }
    };
    vector<thread> th; for (int t = 0; t < threads; t++) th.emplace_back(work); for (auto &t : th) t.join();
    return ok;
}

static double secs() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }
static double median(vector<double> v) { sort(v.begin(), v.end()); size_t n = v.size(); return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]); }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: hs_lzav FILE [--block-mb B] [--sets K] [--codec C] [--runs R] [--threads 1,2,4]\n"); return 1; }
    string path = argv[1]; double bmb = 4; int K = 3, runs = 5; string codec = "lzav", tl = "1,2";
    for (int i = 2; i + 1 < argc; i += 2) {
        string a = argv[i];
        if (a == "--block-mb") bmb = atof(argv[i + 1]); else if (a == "--sets") K = atoi(argv[i + 1]);
        else if (a == "--codec") codec = argv[i + 1]; else if (a == "--runs") runs = atoi(argv[i + 1]);
        else if (a == "--threads") tl = argv[i + 1];
    }
    FILE *f = fopen(path.c_str(), "rb"); if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); uint64_t n = ftell(f); fseek(f, 0, SEEK_SET);
    vector<uint8_t> src(n + 64); if (fread(src.data(), 1, n, f) != n) return 1; fclose(f);
    vector<int> TL; for (size_t p = 0; p < tl.size();) { size_t q = tl.find(',', p); if (q == string::npos) q = tl.size(); TL.push_back(atoi(tl.substr(p, q - p).c_str())); p = q + 1; }
    int maxT = *max_element(TL.begin(), TL.end());
    size_t B = (size_t)(bmb * 1048576);

    vector<double> ct; Packed P;
    for (int r = 0; r < runs; r++) { double t0 = secs(); P = compress(src.data(), n, B, K, codec, maxT); ct.push_back(secs() - t0); }
    size_t multi = 0; { int ns = 1 + *max_element(P.lab.begin(), P.lab.end()); auto SW = set_windows(P.lab, ns); vector<Piece> pc;
        for (auto &b : P.blocks) { pieces(SW[b.set], n, b.stream_off, b.raw_len, pc); if (pc.size() > 1) multi++; } }
    int nsets = 1 + *max_element(P.lab.begin(), P.lab.end());
    const char *bn = strrchr(path.c_str(), '/'); bn = bn ? bn + 1 : path.c_str();
    printf("%s codec=%s block=%.0fMB sets_max=%d sets_used=%d blocks=%zu (non-contiguous %zu) orig=%llu comp=%zu (payload %zu + map/table %zu) ratio=%.3fx c_time(%dT)=%.3fs c_MB/s=%.1f\n",
           bn, codec.c_str(), bmb, K, nsets, P.blocks.size(), multi, (unsigned long long)n, P.total(), P.payload.size(), P.header_bytes(),
           (double)n / P.total(), maxT, median(ct), n / median(ct) / 1e6);
    vector<uint8_t> dst(n + 64);
    for (int T : TL) {
        vector<double> dt; bool ok = true;
        for (int r = 0; r < runs; r++) {
            memset(dst.data(), 0, n);
            double t0 = secs(); bool k = decompress(P, dst.data(), T); dt.push_back(secs() - t0);
            if (!k || memcmp(dst.data(), src.data(), n) != 0) ok = false;
        }
        printf("  decode threads=%d d_time=%.4fs d_MB/s=%.1f verify=%s\n", T, median(dt), n / median(dt) / 1e6, ok ? "OK (byte-for-byte)" : "FAIL");
        fflush(stdout);
        if (!ok) return 3;
    }
    return 0;
}

/*
MIT License

Copyright (c) 2026 Michael Hugh Harrington

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
