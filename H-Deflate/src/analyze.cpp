// analyze.cpp - look at how a non-zlib deflate stream differs from our prediction (development tool).
// usage: analyze FILE [stream_index] [params: level mem]
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#include "hd_search.h"
#include "hd_inflate_fast.h"
#include <cstdio>
#include <map>
using namespace hd;

static std::vector<uint8_t> read_file(const char* fn) { FILE* f = fopen(fn, "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); std::vector<uint8_t> v(n); fread(v.data(), 1, n, f); fclose(f); return v; }

// brute-force longest match at pos within window (max_dist), nearest among equals
static std::pair<int,int> brute(const std::vector<uint8_t>& raw, size_t pos, int max_dist) {
    int best = 0, bd = 0; size_t N = raw.size();
    for (size_t d = 1; d <= (size_t)max_dist && d <= pos; d++) {
        size_t p = pos - d; int l = 0; while (l < 258 && pos + l < N && raw[p + l] == raw[pos + l]) l++;
        if (l > best) { best = l; bd = (int)d; }
    }
    return {best, bd};
}

int main(int argc, char** argv) {
    std::vector<uint8_t> f = read_file(argv[1]);
    int want = argc > 2 ? atoi(argv[2]) : -1;
    // find zlib streams / zip entries quickly: reuse a simple scan: zlib headers only + zip
    std::vector<std::pair<size_t, int>> starts;   // (deflate data pos, wbits hint)
    for (size_t i = 0; i + 8 < f.size(); i++) {
        if (!memcmp(&f[i], "PK\x03\x04", 4)) { uint16_t nl = f[i+26] | (f[i+27] << 8), el = f[i+28] | (f[i+29] << 8); if ((f[i+8] | (f[i+9] << 8)) == 8) starts.push_back({i + 30 + nl + el, 0}); i += 29; continue; }
        uint8_t c = f[i], g = f[i+1];
        if ((c & 15) == 8 && (c >> 4) <= 7 && ((c << 8) | g) % 31 == 0 && !(g & 0x20)) starts.push_back({i + 2, (c >> 4) + 8});
    }
    int idx = 0;
    for (auto& s : starts) {
        Inflated inf; if (!inflate_record(&f[s.first], f.size() - s.first, inf) || inf.comp_len < 2000) continue;
        if (want >= 0 && idx != want) { idx++; continue; }
        printf("stream %d at %zu: comp %zu raw %zu tokens %zu blocks %zu\n", idx, s.first, inf.comp_len, inf.raw.size(), inf.tok.size(), inf.blocks.size());
        // block sizes
        printf("  block token counts:"); for (size_t b = 0; b < std::min<size_t>(8, inf.blocks.size()); b++) printf(" %zu(t%d)", inf.blocks[b].tok_end - inf.blocks[b].tok_begin, inf.blocks[b].type); printf("\n");
        // match stats
        std::map<int,int> lens; int maxd = 0; size_t nm = 0, nl = 0;
        for (auto& t : inf.tok) { if (t.dist) { nm++; lens[t.len]++; if (t.dist > maxd) maxd = t.dist; } else nl++; }
        printf("  literals %zu matches %zu maxdist %d len3 %d len4 %d len258 %d\n", nl, nm, maxd, lens[3], lens[4], lens[258]);
        // how often is the actual match the brute-force longest? (sample)
        size_t pos = 0, samp = 0, longest = 0, shorter = 0, farther = 0, lit_though_match = 0;
        for (size_t i = 0; i < inf.tok.size() && samp < 3000; i++) {
            const Token& t = inf.tok[i];
            if ((i % 7) == 0) {
                auto b = brute(inf.raw, pos, 32768); samp++;
                if (t.dist) { if (t.len == b.first) { longest++; if (t.dist != b.second) farther++; } else if (t.len < b.first) shorter++; }
                else if (b.first >= 3) lit_though_match++;
            }
            pos += t.dist ? t.len : 1;
        }
        printf("  sampled %zu: actual==longest %zu (of which other distance %zu), shorter than longest %zu, literal though match>=3 exists %zu\n", samp, longest, farther, shorter, lit_though_match);
        // best params and correction kinds
        SearchResult sr;
        if (argc > 3 && !strcmp(argv[3], "rank")) { RankParams q; q.lazy = atoi(argv[4]); q.min_len = atoi(argv[5]); q.far3 = atoi(argv[6]); if (argc > 7) q.litcost = atoi(argv[7]); Pack2Result pr = pack_stream2(inf, &f[s.first], q); sr.ok = pr.ok; sr.rank = true; sr.RP = q; sr.rec = pr.rec; sr.corrections = pr.corrections; sr.est_bytes = pr.est_bits / 8; }
        else if (argc > 3) { Params P = zlib_params(atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 8, 15, argc > 5 ? atoi(argv[5]) : 0); if (argc > 6) P.too_far = atoi(argv[6]); PackResult pr = pack_stream(inf, &f[s.first], P); sr.ok = pr.ok; sr.P = P; sr.rec = pr.rec; sr.corrections = pr.corrections; }
        else sr = search_params(inf, &f[s.first], s.second, "x", nullptr, 2);
        printf("  best: %s rec %zu corr %zu exact %d\n", sr.name().c_str(), sr.rec.size(), sr.corrections, sr.exact);
        if (sr.rank) {
            VarReader r(sr.rec.data(), sr.rec.size()); r.byte(); get_rank_params(r); r.get(); size_t nblk = r.get(); r.i += nblk; size_t nk = r.get();
            std::map<int,int> h; for (size_t k = 0; k < nk; k++) h[r.byte()]++;
            printf("  kinds:"); for (auto& kv : h) printf(" %d:%d", kv.first, kv.second); printf("  (0 same, 1 lit, 2+ rank, 10+ shorter, 20 explicit, 21 far)\n");
            size_t nl = r.get(); r.i += nl; size_t ne = r.get(); printf("  blk %zu kind %zu lens %zu expl %zu est %.0f bytes\n", nblk, nk, nl, ne, sr.est_bytes);
        } else {
            VarReader r(sr.rec.data(), sr.rec.size()); r.byte(); get_params(r); size_t nb = r.get();
            std::map<int,int> kinds; int lenShorter = 0, lenLonger = 0, distNearer = 0, distFarther = 0; std::map<int,int> mlen; int dfar1 = 0, dfar2 = 0, dfar3 = 0;
            for (size_t b = 0; b < nb; b++) {
                uint8_t flags = r.byte(); if (flags & F_COUNT) r.get();
                if (flags & F_HDR) { size_t nb2 = r.get(); for (size_t k = 0; k < (nb2 + 7) / 8; k++) r.byte(); }
                if (flags & F_ALT) { size_t na = r.get(); for (size_t k = 0; k < na; k++) r.get(); }
                size_t nc = r.get();
                for (size_t k = 0; k < nc; k++) { r.get(); int kind = r.byte(); kinds[kind]++; if (kind == C_MATCH || kind == C_BOTH) { size_t l = r.get() + 3; size_t d = r.get() + 1; if (kind == C_MATCH) { mlen[l > 5 ? 6 : l]++; if (d > 32506) dfar3++; else if (d > 4096) dfar2++; else dfar1++; } } else if (kind == C_LEN) { r.get(); } else if (kind == C_DIST) r.get(); }
            }
            printf("  corrections: lit-instead-of-match %d, match-instead-of-lit %d, other-len %d, other-dist %d, both %d\n", kinds[C_LIT], kinds[C_MATCH], kinds[C_LEN], kinds[C_DIST], kinds[C_BOTH]);
            printf("  match-instead-of-lit: len3 %d len4 %d len5 %d len6+ %d | dist<=4096 %d, 4097..32506 %d, >32506 %d\n", mlen[3], mlen[4], mlen[5], mlen[6], dfar1, dfar2, dfar3);
            (void)lenShorter; (void)lenLonger; (void)distNearer; (void)distFarther;
        }
        idx++;
        if (want >= 0) break;
        if (idx > 6) break;
    }
}
