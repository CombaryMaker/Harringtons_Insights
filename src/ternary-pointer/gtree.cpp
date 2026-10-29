// Generic-tree context-mixing test engine (v3-style model) for ternary experiments.
// Each byte value is coded as a path of binary decisions down a tree. Trees:
//   bin8      : the normal 8-bit binary tree (control)
//   tern6:XXXXXX : 6 base-3 digits per byte; X = value split off first at each digit (0,1,2)
//   rtern6    : same, after ranking bytes by frequency (rank 0 = most common)  [+256-byte header]
//   thuff     : ternary Huffman tree from byte counts, each 3-way node = 2 decisions [+header]
//   bhuff     : binary Huffman tree from byte counts (control for thuff) [+header]
// Reports ideal code length (sum of -log2 p) per file, as 1,000,000 bits down to, and
// decisions per byte.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <string>
#include <functional>
#include <array>

static int16_t STI[4096], SQI[4096];
static int RT15[256];
static void init_tables() {
    for (int p = 0; p < 4096; p++) { int q = p ? p : 1; double v = log((double)q / (4096 - q)) * 256; int s = (int)(v < 0 ? v - 0.5 : v + 0.5); STI[p] = (int16_t)(s > 2047 ? 2047 : (s < -2047 ? -2047 : s)); }
    for (int x = -2048; x < 2048; x++) { int p = (int)(4096.0 / (1.0 + exp(-x / 256.0))); SQI[x + 2048] = (int16_t)(p < 1 ? 1 : (p > 4095 ? 4095 : p)); }
    for (int n = 0; n < 256; n++) RT15[n] = (int)(32768.0 / (n + 1.6));
}

// ---- tree: nodes 0..NN-1; child[node][bit] >= 0 node, < 0 leaf (value = -1 - child)
struct Tree {
    std::vector<int> c0, c1;              // children
    std::vector<int> path_node[256], path_bit[256];
    int header_bytes = 0;
    int add() { c0.push_back(0); c1.push_back(0); return (int)c0.size() - 1; }
    void build_paths() {
        for (int v = 0; v < 256; v++) { path_node[v].clear(); path_bit[v].clear(); }
        std::vector<std::pair<int, std::vector<std::pair<int,int>>>> st;
        std::vector<std::pair<int,int>> pth;
        walk(0, pth);
    }
    void walk(int node, std::vector<std::pair<int,int>> &pth) {
        for (int b = 0; b < 2; b++) {
            int ch = b ? c1[node] : c0[node];
            pth.push_back({node, b});
            if (ch < 0) { int v = -1 - ch; for (auto &q : pth) { path_node[v].push_back(q.first); path_bit[v].push_back(q.second); } }
            else walk(ch, pth);
            pth.pop_back();
        }
    }
};

// binary 8-bit tree: node id = partial byte c0 (1..255) - 1
static Tree make_bin8() {
    Tree t; for (int i = 0; i < 255; i++) t.add();
    for (int c0 = 1; c0 < 256; c0++) {
        int n = c0 - 1;
        for (int b = 0; b < 2; b++) { int nx = c0 * 2 + b; int ch = nx < 256 ? nx - 1 : -1 - (nx - 256); if (b) t.c1[n] = ch; else t.c0[n] = ch; }
    }
    t.build_paths(); return t;
}

typedef std::vector<int> VI;

static int leaf_or_node(Tree &t, const VI &vals, std::function<int(const VI&)> sub) {
    if (vals.size() == 1) return -1 - vals[0];
    return sub(vals);
}

// a 3-way split coded as: decision A = "in group F?" ; if not, decision B = "group X vs group Y"
// empty groups are skipped (degenerate splits collapse to fewer decisions)
static int split3(Tree &t, std::vector<VI> g, int F, std::function<int(const VI&)> sub) {
    std::vector<VI> ne; std::vector<int> idx;
    for (int k = 0; k < 3; k++) if (!g[k].empty()) { ne.push_back(g[k]); idx.push_back(k); }
    if (ne.size() == 1) return sub(ne[0]);
    if (ne.size() == 2) { int n = t.add(); int a = leaf_or_node(t, ne[0], sub), b = leaf_or_node(t, ne[1], sub); t.c0[n] = a; t.c1[n] = b; return n; }
    // three non-empty groups
    VI rest; int o1 = -1, o2 = -1;
    for (int k = 0; k < 3; k++) if (k != F) { if (o1 < 0) o1 = k; else o2 = k; }
    int n = t.add();
    int a = leaf_or_node(t, g[F], sub);
    int m = t.add();
    int b0 = leaf_or_node(t, g[o1], sub), b1 = leaf_or_node(t, g[o2], sub);
    t.c0[m] = b0; t.c1[m] = b1;
    t.c0[n] = m; t.c1[n] = a;       // bit 1 = "in group F"
    return n;
}

static int TOFF = 0;
// hybrid: top K bits as binary decisions, then base-3 digits of the low (8-K) bits
static Tree make_hybrid(int K, const int *first) {
    Tree t;
    int low = 8 - K; int D = 0; { int r = 1; while (r < (1 << low)) { r *= 3; D++; } }
    std::function<int(const VI&, int)> rec;
    rec = [&](const VI &vals, int lvl) -> int {
        if (vals.size() == 1) return -1 - vals[0];
        if (lvl < K) {
            VI a, b; for (int v : vals) ((v >> (7 - lvl)) & 1 ? b : a).push_back(v);
            if (a.empty()) return rec(b, lvl + 1);
            if (b.empty()) return rec(a, lvl + 1);
            int n = t.add(); int ca = rec(a, lvl + 1), cb = rec(b, lvl + 1); t.c0[n] = ca; t.c1[n] = cb; return n;
        }
        int d = lvl - K;
        std::vector<VI> g(3);
        for (int v : vals) { int code = v & ((1 << low) - 1); int p3 = 1; for (int i = 0; i < D - 1 - d; i++) p3 *= 3; g[(code / p3) % 3].push_back(v); }
        return split3(t, g, first[d], [&](const VI &s) { return rec(s, lvl + 1); });
    };
    VI all; for (int v = 0; v < 256; v++) all.push_back(v);
    int root = rec(all, 0);
    if (root != 0) { fprintf(stderr, "hyb root not 0\n"); exit(1); }
    t.build_paths(); return t;
}
static Tree make_tern6(const int *first, const int *rank_of /* nullptr = identity */) {
    Tree t;
    // value -> code (rank or byte); digits of code
    std::function<int(const VI&, int)> rec;
    rec = [&](const VI &vals, int d) -> int {
        if (vals.size() == 1) return -1 - vals[0];
        std::vector<VI> g(3);
        for (int v : vals) { int code = rank_of ? rank_of[v] : ((v + TOFF) & 255); int digit = (code / (int)pow(3, 5 - d)) % 3; g[digit].push_back(v); }
        return split3(t, g, first[d], [&](const VI &s) { return rec(s, d + 1); });
    };
    VI all; for (int v = 0; v < 256; v++) all.push_back(v);
    int root = rec(all, 0);
    if (root != 0) { fprintf(stderr, "root not 0\n"); exit(1); }
    t.build_paths();
    if (rank_of) t.header_bytes = 256;
    return t;
}

// Huffman trees (k = 2 or 3) from counts (+1 smoothing); each internal node splits into k groups
static Tree make_huff(const std::vector<uint64_t> &cnt, int k) {
    struct H { uint64_t w; VI vals; std::vector<int> kids; };
    std::vector<H> nodes;
    typedef std::pair<uint64_t,int> P;
    std::vector<P> heap;
    for (int v = 0; v < 256; v++) { nodes.push_back({cnt[v] + 1, {v}, {}}); heap.push_back({cnt[v] + 1, v}); }
    auto cmp = [](const P &a, const P &b) { return a.first > b.first; };
    std::make_heap(heap.begin(), heap.end(), cmp);
    if (k == 3 && (256 - 1) % 2 != 0) {}   // 256 leaves: (256-1)%(3-1)=1 -> ok for ternary without dummies
    bool firstmerge = true;
    while (heap.size() > 1) {
        int take = std::min((int)heap.size(), k);
        if (k == 3 && firstmerge) take = 2;   // 256 leaves: one 2-way merge makes the ternary tree full
        firstmerge = false;
        H nn{0, {}, {}};
        for (int i = 0; i < take; i++) { std::pop_heap(heap.begin(), heap.end(), cmp); P p = heap.back(); heap.pop_back(); nn.w += p.first; nn.kids.push_back(p.second); for (int v : nodes[p.second].vals) nn.vals.push_back(v); }
        nodes.push_back(nn); heap.push_back({nn.w, (int)nodes.size() - 1}); std::push_heap(heap.begin(), heap.end(), cmp);
    }
    int rootH = heap[0].second;
    Tree t;
    std::function<int(int)> rec = [&](int h) -> int {
        if (nodes[h].kids.empty()) return -1 - nodes[h].vals[0];
        auto &kd = nodes[h].kids;   // kd[0] lightest ... (popped in increasing weight)
        if (kd.size() == 2) { int n = t.add(); int a = rec(kd[1]), b = rec(kd[0]); t.c0[n] = a; t.c1[n] = b; return n; }
        // 3 kids: isolate the heaviest first (kd[2]); then kd[1] vs kd[0]
        int n = t.add(); int m = t.add();
        int a = rec(kd[2]); int b0 = rec(kd[1]), b1 = rec(kd[0]);
        t.c0[m] = b0; t.c1[m] = b1; t.c0[n] = m; t.c1[n] = a;
        return n;
    };
    int root = rec(rootH);
    if (root != 0) { fprintf(stderr, "huff root not 0\n"); exit(1); }
    t.build_paths();
    t.header_bytes = 160;   // code lengths for 256 symbols, about 5 bits each
    return t;
}

// ---- model (v3-like, hashed per decision without buckets)
static const int TB = 22; static const uint32_t TM = (1u << TB) - 1;
enum { NORD = 6, NH = NORD + 2, NM = NH + 1, NIN = NM + 2 };
static const int ORD[NORD] = {1, 2, 3, 4, 6, 8};
static inline uint32_t mix32(uint32_t x) { x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15; return x; }

static double code_file(const std::vector<uint8_t> &d, const Tree &t, double *dec_per_byte) {
    int NN = (int)t.c0.size();
    std::vector<uint32_t> T[NM];
    for (int i = 0; i < NH; i++) T[i].assign((size_t)1 << TB, 0x8000);
    T[NH].assign(NN, 0x8000);
    std::vector<int32_t> W((size_t)NN * 4 * 2 * NIN, 19661);
    std::vector<uint16_t> A((size_t)NN * 256 * 33);
    std::vector<uint8_t> Aok((size_t)NN * 256, 0);
    uint16_t row0[33]; for (int j = 0; j < 33; j++) { int x = j * 128 - 2048; if (x > 2047) x = 2047; row0[j] = (uint16_t)(SQI[x + 2048] * 16); }
    std::vector<int32_t> mtab(1 << 20, 0);
    uint16_t mm[64]; for (int i = 0; i < 64; i++) mm[i] = 49152;
    uint32_t h[NH]; uint32_t word = 0, pword = 0; int64_t mptr = 0, mlen = 0; int last = 0;
    double bits = 0; uint64_t decisions = 0;
    auto hashes = [&](size_t n) {
        uint32_t x = 0x9E3779B1u; int k = 0;
        for (int j = 1; j <= 8; j++) { uint32_t c = n >= (size_t)j ? d[n - j] : 0; x = (x ^ c) * 0x01000193u + (uint32_t)j; if (j == ORD[k]) { h[k] = (x + (uint32_t)j * 0x6A09E667u) * 0x2545F491u; k++; } }
        h[NORD] = word * 0x6F4F2A35u + 0x51ED27u; h[NORD + 1] = (pword * 0x3B9AC9FBu) ^ (word * 0x6F4F2A35u) ^ 0x7A3D1u;
    };
    hashes(0);
    for (size_t pos = 0; pos < d.size(); pos++) {
        int v = d[pos];
        int mbyte = (mlen > 0 && (size_t)mptr < pos) ? d[mptr] : -1;
        int mk = mlen < 63 ? (int)mlen : 63, ms = mlen == 0 ? 0 : mlen < 16 ? 1 : mlen < 32 ? 2 : 3;
        int letter = ((last | 32) >= 'a' && (last | 32) <= 'z');
        const std::vector<int> &pn = t.path_node[v], &pb = t.path_bit[v];
        const std::vector<int> *mpn = mbyte >= 0 ? &t.path_node[mbyte] : nullptr; const std::vector<int> *mpb = mbyte >= 0 ? &t.path_bit[mbyte] : nullptr;
        bool mok = mbyte >= 0;
        for (size_t s = 0; s < pn.size(); s++) {
            int node = pn[s], y = pb[s];
            uint32_t *slot[NM]; int x[NIN];
            for (int i = 0; i < NH; i++) slot[i] = &T[i][mix32(h[i] + (uint32_t)node * 0x9E3779B1u) & TM];
            slot[NH] = &T[NH][node];
            for (int i = 0; i < NM; i++) x[i] = STI[(*slot[i] & 0xFFFF) >> 4];
            int mexp = -1;
            if (mok) { if (s < mpn->size() && (*mpn)[s] == node) mexp = (*mpb)[s]; else mok = false; }
            if (mexp >= 0) { int st = STI[mm[mk] >> 4]; x[NM] = mexp ? st : -st; } else x[NM] = 0;
            x[NM + 1] = 64;
            int32_t *ws = &W[((size_t)node * 8 + ms * 2 + letter) * NIN];
            int64_t dot = 0; for (int i = 0; i < NIN; i++) dot += (int64_t)ws[i] * x[i];
            int sd = (int)(dot >> 16); sd = sd > 2047 ? 2047 : (sd < -2047 ? -2047 : sd);
            int pm = SQI[sd + 2048];
            size_t ar = (size_t)node * 256 + last; uint16_t *row = &A[ar * 33];
            if (!Aok[ar]) { memcpy(row, row0, sizeof row0); Aok[ar] = 1; }
            int apos = sd + 2048, j = apos >> 7, wt = apos & 127;
            int pa = (row[j] * (128 - wt) + row[j + 1] * wt) >> 11;
            int p = (pm * 26 + pa * 230) >> 8; p = p < 1 ? 1 : (p > 4095 ? 4095 : p);
            bits += -log2(y ? p / 4096.0 : 1 - p / 4096.0); decisions++;
            int err = (y << 12) - pm; for (int i = 0; i < NIN; i++) ws[i] += (x[i] * err + 1024) >> 11;
            int tgt = y ? 65535 : 0;
            for (int i = 0; i < NM; i++) { uint32_t e = *slot[i]; int pp = e & 0xFFFF, nn = e >> 16; pp += ((tgt - pp) * RT15[nn]) >> 15; nn = nn < (i == 0 ? 60 : 255) ? nn + 1 : nn; *slot[i] = (uint32_t)pp | ((uint32_t)nn << 16); }
            if (mexp >= 0) { int vv = mm[mk]; int hit = y == mexp ? 65535 : 0; mm[mk] = (uint16_t)(vv + ((hit - vv) >> 5)); }
            int t16 = y ? 65535 : 0;
            row[j] = (uint16_t)(row[j] + (((t16 - row[j]) * (128 - wt)) >> 13)); row[j + 1] = (uint16_t)(row[j + 1] + (((t16 - row[j + 1]) * wt) >> 13));
        }
        size_t nb = pos + 1;
        if ((v >= 65 && v <= 90) || (v >= 97 && v <= 122)) word = (word * 0x2C9277B5u) ^ (uint32_t)(v | 32); else { if (word) pword = word; word = 0; }
        if (mlen > 0 && (size_t)mptr < nb - 1 && d[mptr] == v) { mptr++; mlen++; } else mlen = 0;
        if (nb >= 6) {
            uint32_t hh = 0; for (int j2 = 1; j2 <= 6; j2++) hh = (uint32_t)(((uint64_t)hh * 0x2F0B4A3u + d[nb - j2]) & 0xFFFFF);
            if (mlen == 0) { int64_t cand = mtab[hh]; if (cand > 0) { int L = 0; while (L < 32 && cand - 1 - L >= 0 && d[cand - 1 - L] == d[nb - 1 - L]) L++; if (L >= 6) { mptr = cand; mlen = L; } } }
            mtab[hh] = (int32_t)nb;
        }
        last = v; hashes(nb);
    }
    *dec_per_byte = d.empty() ? 0 : (double)decisions / d.size();
    return bits / 8 + t.header_bytes + 13;
}

int main(int argc, char **argv) {
    init_tables();
    std::string spec = argv[1];
    double tot = 0, tin = 0;
    for (int a = 2; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); std::vector<uint8_t> d; int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); fclose(f);
        std::vector<uint64_t> cnt(256, 0); for (uint8_t b : d) cnt[b]++;
        Tree t;
        if (spec == "bin8") t = make_bin8();
        else if (spec.rfind("hyb", 0) == 0) { int K = spec[3] - '0'; int first[6] = {0,0,0,0,0,0}; size_t c = spec.find(':'); if (c != std::string::npos) for (size_t i = 0; c + 1 + i < spec.size() && i < 6; i++) first[i] = spec[c + 1 + i] - '0'; t = make_hybrid(K, first); }
        else if (spec.rfind("tern6o", 0) == 0) { TOFF = atoi(spec.c_str() + 6); size_t c = spec.find(':'); int first[6]; for (int i = 0; i < 6; i++) first[i] = spec[c + 1 + i] - '0'; t = make_tern6(first, nullptr); }
        else if (spec.rfind("tern6:", 0) == 0) { int first[6]; for (int i = 0; i < 6; i++) first[i] = spec[6 + i] - '0'; t = make_tern6(first, nullptr); }
        else if (spec.rfind("rtern6:", 0) == 0) {
            int first[6]; for (int i = 0; i < 6; i++) first[i] = spec[7 + i] - '0';
            int order[256]; for (int v = 0; v < 256; v++) order[v] = v;
            std::stable_sort(order, order + 256, [&](int x, int y) { return cnt[x] > cnt[y]; });
            static int rank_of[256]; for (int r = 0; r < 256; r++) rank_of[order[r]] = r;
            t = make_tern6(first, rank_of);
        }
        else if (spec == "thuff") t = make_huff(cnt, 3);
        else if (spec == "bhuff") t = make_huff(cnt, 2);
        double dpb;
        double bytes = code_file(d, t, &dpb);
        const char *b = strrchr(argv[a], '/'); b = b ? b + 1 : argv[a];
        printf("%-13s %8.0f bytes  per-million %8.0f  decisions-per-byte %.2f\n", b, bytes, bytes / d.size() * 1e6, dpb);
        tot += bytes; tin += d.size();
    }
    printf("TOTAL %s %.0f per-million %.0f\n", spec.c_str(), tot, tot / tin * 1e6);
    return 0;
}
