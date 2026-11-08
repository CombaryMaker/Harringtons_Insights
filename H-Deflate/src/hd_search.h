// hd_search.h - parameter search per stream: likely settings first (learned from earlier wins), score on a prefix,
// run the best few in full, keep the smallest record. Part of H-Deflate.
// Copyright (c) 2026 Michael Hugh Harrington. MIT License, see H-Deflate/LICENSE.txt.
#pragma once
#include "hd_rank.h"
#include <map>
#include <mutex>

namespace hd {

struct Prior {   // per container kind: which settings won before (his "learned starting prediction" idea)
    std::mutex mu;
    std::map<std::string, std::map<std::string, int>> wins;   // kind -> param name -> count
    void add(const std::string& kind, const Params& P) { std::lock_guard<std::mutex> g(mu); wins[kind][P.name()]++; }
    std::vector<std::string> order(const std::string& kind) {
        std::lock_guard<std::mutex> g(mu); std::vector<std::pair<int, std::string>> v;
        for (auto& kv : wins[kind]) v.push_back({kv.second, kv.first});
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::vector<std::string> o; for (auto& e : v) o.push_back(e.second); return o;
    }
};

inline std::vector<Params> candidate_list(int wbits_hint, int depth) {
    std::vector<Params> c;
    auto add = [&](Params p) { c.push_back(p); };
    int wb = wbits_hint ? wbits_hint : 15;
    // zlib table, most common first
    for (int lv : {6, 9, 1, 2, 3, 4, 5, 7, 8}) add(zlib_params(lv, 8, wb, 0));
    for (int lv : {9, 6, 1, 2, 3, 4, 5, 7, 8}) add(zlib_params(lv, 8, wb, 1));   // filtered
    if (depth >= 1) {
        for (int m : {9, 7, 6, 5, 4, 3, 2, 1}) for (int lv : {6, 9, 1, 4, 5, 7, 8, 2, 3}) add(zlib_params(lv, m, wb, 0));
        for (int m : {9, 7}) for (int lv : {9, 6, 1, 4, 5, 7, 8, 2, 3}) add(zlib_params(lv, m, wb, 1));
        for (int lv : {6, 9, 1}) add(zlib_params(lv, 8, wb, 4));   // fixed
        { Params p = zlib_params(6, 8, wb, 3); add(p); }           // rle
        { Params p = zlib_params(6, 8, wb, 2); add(p); }           // huffman only
        add(zlib_params(0, 8, wb, 0));                              // stored
        if (!wbits_hint) for (int w : {14, 13, 12, 11, 10, 9}) for (int lv : {6, 9}) add(zlib_params(lv, 8, w, 0));
    }
    if (depth >= 2) {   // beyond the zlib table: wider chains and lazy thresholds (his "look further" rule)
        for (int chain : {8192, 16384, 32768}) for (int lazy : {258, 128}) { Params p = zlib_params(9, 8, wb, 0); p.chain = chain; p.lazy = lazy; p.level_hint = 9; add(p); }
        for (int hb : {16, 14, 13, 12}) { Params p = zlib_params(9, 8, wb, 0); p.hash_bits = hb; add(p); }
        for (int lv : {6, 9}) { Params p = zlib_params(lv, 8, wb, 0); p.too_far = false; add(p); }
        for (int lv : {6, 9}) { Params p = zlib_params(lv, 8, wb, 0); p.stale = false; add(p); }
        for (int nice : {64, 32, 16}) { Params p = zlib_params(9, 8, wb, 0); p.nice = nice; add(p); }
    }
    return c;
}

struct SearchResult { bool ok = false; Params P; RankParams RP; bool rank = false; std::vector<uint8_t> rec; size_t corrections = 0; int tried = 0; bool exact = false; double est_bytes = 0; std::string name() const { return rank ? RP.name() : P.name(); } };

// Find the best record for stream R (comp = its deflate bytes). kind = container kind for priors.
inline size_t comp_len_hint(const Inflated& R) { return R.comp_len; }

inline SearchResult search_params(const Inflated& R, const uint8_t* comp, int wbits_hint, const std::string& kind, Prior* prior, int depth = 2) {
    SearchResult best;
    std::vector<Params> cands = candidate_list(wbits_hint, depth);
    // order: prior winners first
    if (prior) {
        std::vector<std::string> o = prior->order(kind);
        std::vector<Params> front;
        for (auto& name : o) for (auto& p : cands) if (p.name() == name) { front.push_back(p); break; }
        std::vector<Params> rest;
        for (auto& p : cands) { bool f = false; for (auto& q : front) if (q.name() == p.name()) { f = true; break; } if (!f) rest.push_back(p); }
        cands = front; cands.insert(cands.end(), rest.begin(), rest.end());
    }
    size_t total_tok = R.tok.size();
    size_t prefix = std::min<size_t>(total_tok, 4000);
    struct Sc { size_t rec; size_t corr; int i; };
    std::vector<Sc> scores;
    size_t tried = 0;
    for (size_t i = 0; i < cands.size(); i++) {
        PackResult pr = pack_stream(R, comp, cands[i], prefix);
        tried++;
        if (!pr.ok) continue;
        scores.push_back({pr.rec.size(), pr.corrections, (int)i});
        if (pr.corrections == 0 && pr.tokens_done >= total_tok) {   // whole stream, no corrections
            PackResult full = pr.complete ? pr : pack_stream(R, comp, cands[i]);
            if (!full.ok || !full.complete) continue;
            best.ok = true; best.P = cands[i]; best.rec = full.rec; best.corrections = full.corrections; best.exact = full.corrections == 0; best.tried = (int)tried; best.est_bytes = full.rec.size() * 0.85;
            if (!best.exact) break;   // fall through to the ordinary comparison below
            if (prior) prior->add(kind, cands[i]);
            return best;
        }
        if (pr.corrections == 0 && tried >= 3) break;   // zero corrections on the prefix: likely exact, confirm below
    }
    if (scores.empty()) return best;
    std::sort(scores.begin(), scores.end(), [](const Sc& a, const Sc& b) { return a.rec != b.rec ? a.rec < b.rec : a.i < b.i; });
    size_t top = std::min<size_t>(scores.size(), 3);
    for (size_t k = 0; k < top; k++) {
        const Params& P = cands[scores[k].i];
        PackResult full = pack_stream(R, comp, P);
        if (!full.ok) continue;
        if (!best.ok || full.rec.size() < best.rec.size()) { best.ok = true; best.P = P; best.rec = full.rec; best.corrections = full.corrections; }
        if (full.corrections == 0) break;
    }
    best.tried = (int)tried;
    best.exact = best.ok && best.corrections == 0;
    if (best.ok) best.est_bytes = best.rec.size() * 0.85;
    // not close to exact: try the rank predictor (record v2) and keep whichever should cost less after compression
    if (depth >= 2 && (!best.ok || best.rec.size() > 0.01 * (double)comp_len_hint(R))) {
        std::vector<RankParams> rc;
        for (int lazy : {1, 0}) for (int ml : {3, 4}) for (int f3 : {32768, 4096, 1024, 256}) { RankParams q; q.wbits = wbits_hint ? wbits_hint : 15; q.lazy = lazy; q.min_len = ml; q.far3 = f3; if (ml == 4 && f3 != 32768) continue; rc.push_back(q); }
        struct S2 { double bits; int i; }; std::vector<S2> sc;
        for (size_t i = 0; i < rc.size(); i++) { Pack2Result pr = pack_stream2(R, comp, rc[i], prefix); if (pr.ok) sc.push_back({pr.est_bits, (int)i}); }
        std::sort(sc.begin(), sc.end(), [](const S2& a, const S2& b) { return a.bits < b.bits; });
        for (size_t k = 0; k < std::min<size_t>(sc.size(), 2); k++) {
            Pack2Result pr = pack_stream2(R, comp, rc[sc[k].i]);
            if (!pr.ok || !pr.complete) continue;
            double est = pr.est_bits / 8;
            if (!best.ok || est < best.est_bytes) { best.ok = true; best.rank = true; best.RP = rc[sc[k].i]; best.rec = pr.rec; best.corrections = pr.corrections; best.est_bytes = est; best.exact = false; }
        }
    }
    if (best.ok && prior && !best.rank) prior->add(kind, best.P);
    return best;
}

} // namespace hd
