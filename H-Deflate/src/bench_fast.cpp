// bench_fast.cpp - compares inflate_record_ref (v1) with inflate_record_fast (v1.01): same raw bytes, tokens and blocks;
// tokens, same blocks; median of 5 timings each. (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#include "hd_inflate_fast.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
using namespace hd;
static std::vector<uint8_t> rd(const char* f) { FILE* fp = fopen(f, "rb"); std::vector<uint8_t> v; if (!fp) return v; fseek(fp, 0, 2); v.resize(ftell(fp)); fseek(fp, 0, 0); if (fread(v.data(), 1, v.size(), fp)) {} fclose(fp); return v; }
template <class F> double med(F f) { double t[5]; for (int i = 0; i < 5; i++) { auto a = std::chrono::steady_clock::now(); f(); t[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count(); } std::sort(t, t + 5); return t[2]; }
int main(int c, char** v) {
    printf("file,comp_bytes,raw_bytes,orig_s,fast_s,orig_MBps,fast_MBps,speedup,identical\n");
    for (int i = 1; i < c; i++) {
        auto d = rd(v[i]); Inflated a, b;
        double t0 = med([&] { inflate_record_ref(d.data(), d.size(), a); });
        double t1 = med([&] { inflate_record_fast(d.data(), d.size(), b); });
        bool same = a.error.empty() && b.error.empty() && a.raw == b.raw && a.comp_len == b.comp_len && a.tok.size() == b.tok.size() && a.blocks.size() == b.blocks.size();
        for (size_t k = 0; same && k < a.tok.size(); k++) same = a.tok[k].len == b.tok[k].len && a.tok[k].dist == b.tok[k].dist;
        for (size_t k = 0; same && k < a.blocks.size(); k++) same = a.blocks[k].tok_end == b.blocks[k].tok_end && a.blocks[k].raw_end == b.blocks[k].raw_end && a.blocks[k].hdr_nbits == b.blocks[k].hdr_nbits && a.blocks[k].alt258 == b.blocks[k].alt258;
        double mb = a.raw.size() / 1e6;
        printf("%s,%zu,%zu,%.4f,%.4f,%.1f,%.1f,%.2fx,%s\n", v[i], d.size(), a.raw.size(), t0, t1, mb / t0, mb / t1, t0 / t1, same ? "yes" : "NO");
    }
}
