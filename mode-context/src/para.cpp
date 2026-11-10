// para.cpp - paragraph context test for Michael Harrington's idea (10 Nov 2026). Michael Hugh Harrington. CC0 1.0 (public domain), see LICENSE.txt.
// Same small context-mixing model as ctxcm.cpp; size = the model's code length in bytes (+ stored labels where used).
// Base: orders 1,2,3,4,5,6,7 + word + word pair + word triple + sparse (11 guessers).
// Variants (letters can be combined, e.g. "AB", "AB+m"):
//   A  paragraph structure, free (the unpacker sees it): paragraph type from its first character (dialogue or not),
//      the types of the previous two paragraphs, sentence number and word number inside the paragraph. 2 guessers.
//   B  stored paragraph label: the packer reads each whole paragraph, sorts paragraphs into K groups by their make-up
//      (dialogue share, first-person share, sentence length, ! and ? share, comma rate) and stores one label per
//      paragraph; the label cost is counted. 2 guessers.
//   D  A plus 2 more free guessers: speaker turn (1st/3rd/5th vs 2nd/4th... paragraph of a dialogue run) and the
//      previous paragraph's length.
//   C  control: 2 more plain guessers (order 8, word pair with a gap) - is a gain just from having more guessers?
//   +m the blend (mixer weights) is chosen by the paragraph type (A) or label (B) as well as by the bit position.
// usage: para VARIANT FILE [K]
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
using namespace std;
static const int TB = 22;
static inline uint32_t H(uint64_t a, uint64_t b) { uint64_t x = (a + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull ^ (b + 0x94D049BB133111EBull); x ^= x >> 31; x *= 0xD6E8FEB86659FD93ull; x ^= x >> 29; return (uint32_t)x; }
static inline float stretch(float p) { return logf(p / (1 - p)); }
static inline float squash(float x) { return 1.0f / (1.0f + expf(-x)); }
static bool isq(const vector<uint8_t> &d, size_t i) { return d[i] == '"' || (i + 2 < d.size() && d[i] == 0xE2 && d[i + 1] == 0x80 && (d[i + 2] == 0x9C || d[i + 2] == 0x9D)); }
static bool islet(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    string var = argv[1]; bool mixsel = false; { size_t u = var.find("+m"); if (u != string::npos) { mixsel = true; var = var.substr(0, u); } }
    bool D = var.find('D') != string::npos; bool A = var.find('A') != string::npos || D, Bv = var.find('B') != string::npos, C = var.find('C') != string::npos;
    int K = argc > 3 ? atoi(argv[3]) : 4;
    FILE *f = fopen(argv[2], "rb"); if (!f) return 1; vector<uint8_t> d; { int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); } fclose(f);
    size_t n = d.size();
    // paragraphs: split at blank lines (a line holding only \r or nothing)
    vector<size_t> pstart; vector<int> pid(n, 0);
    { pstart.push_back(0); size_t ls = 0; bool prevBlank = false; int cur = 0;
      for (size_t i = 0; i < n; i++) { pid[i] = cur;
          if (d[i] == '\n') { bool blank = (i == ls) || (i == ls + 1 && d[ls] == '\r'); if (blank) prevBlank = true; else if (prevBlank) {} ls = i + 1;
              if (blank) { prevBlank = true; } }
          else if (prevBlank && d[i] != '\r') { cur++; pstart.push_back(i); pid[i] = cur; prevBlank = false; } } }
    int np = (int)pstart.size();
    // B: labels from each paragraph's make-up, k-means into K groups; label cost by an adaptive order-2 model
    vector<int> lab(np, 0); double labbits = 0;
    if (Bv) {
        vector<array<double, 5>> F(np);
        for (int p = 0; p < np; p++) { size_t a = pstart[p], b = p + 1 < np ? pstart[p + 1] : n; double words = 0, quotes = 0, firstp = 0, sents = 0, excl = 0, commas = 0; bool inw = false;
            for (size_t i = a; i < b; i++) { int c = d[i]; if (islet(c)) { if (!inw) { words++; if ((c == 'I' && (i + 1 >= b || !islet(d[i + 1]))) || ((c == 'm' || c == 'M') && i + 1 < b && d[i + 1] == 'y' && (i + 2 >= b || !islet(d[i + 2])))) firstp++; } inw = true; } else inw = false;
                if (isq(d, i)) quotes++; if (c == '.' || c == '!' || c == '?') sents++; if (c == '!' || c == '?') excl++; if (c == ',') commas++; }
            words = max(words, 1.0); sents = max(sents, 1.0);
            F[p] = {min(quotes / words * 8, 1.0), min(firstp / words * 10, 1.0), min(words / sents / 40, 1.0), excl / sents, min(commas / words * 6, 1.0)}; }
        vector<array<double, 5>> cen(K); for (int k = 0; k < K; k++) cen[k] = F[(size_t)np * (2 * k + 1) / (2 * K)];
        for (int it = 0; it < 20; it++) { for (int p = 0; p < np; p++) { double best = 1e9; for (int k = 0; k < K; k++) { double s = 0; for (int j = 0; j < 5; j++) s += (F[p][j] - cen[k][j]) * (F[p][j] - cen[k][j]); if (s < best) { best = s; lab[p] = k; } } }
            for (int k = 0; k < K; k++) { array<double, 5> s{}; int c = 0; for (int p = 0; p < np; p++) if (lab[p] == k) { c++; for (int j = 0; j < 5; j++) s[j] += F[p][j]; } if (c) for (int j = 0; j < 5; j++) cen[k][j] = s[j] / c; } }
        vector<double> cnt(K * K * K, 0.5); for (int p = 0; p < np; p++) { int a = p > 1 ? lab[p - 2] : 0, b = p > 0 ? lab[p - 1] : 0; double *c = &cnt[(a * K + b) * K], tot = 0; for (int k = 0; k < K; k++) tot += c[k]; labbits += -log2(c[lab[p]] / tot); c[lab[p]] += 1; }
    }
    int nin = 11 + (A ? 2 : 0) + (D ? 2 : 0) + (Bv ? 2 : 0) + (C ? 2 : 0);
    vector<vector<uint32_t>> T(nin, vector<uint32_t>(1u << TB, 0x80000000u));
    const int NSEL = 16; vector<float> W(256 * NSEL * (nin + 1), 0.3f); vector<uint32_t> ctx(nin), idx(nin);
    uint64_t hist = 0; uint32_t word = 0, w1 = 0, w2 = 0, w3 = 0; int ptype = 3, ph = 0, run = 0, turn = 0, plen = 0, curlen = 0, sIdx = 0, wIdx = 0, lastp = -1; double bits = 0;
    for (size_t i = 0; i < n; i++) {
        int p = pid[i];
        if (p != lastp) { ph = (ph << 2 | (ptype & 3)) & 15; plen = curlen; curlen = 0; ptype = 2; sIdx = 0; wIdx = 0; lastp = p; }   // type unknown (2) until the paragraph's first character has been coded
        int k = 0;
        for (int o : {1, 2, 3, 4, 5, 6, 7}) ctx[k++] = H(o, hist & ((1ull << (8 * o)) - 1));
        ctx[k++] = H(20, word); ctx[k++] = H(21, (uint64_t)w1 << 32 | word); ctx[k++] = H(22, (uint64_t)w2 << 40 ^ (uint64_t)w1 << 20 ^ word); ctx[k++] = H(23, hist & 0xFF00FF00ull);
        int wb = wIdx == 0 ? 0 : wIdx < 4 ? 1 : wIdx < 12 ? 2 : wIdx < 30 ? 3 : wIdx < 60 ? 4 : 5;
        if (A) { ctx[k++] = H(30, (uint64_t)(ptype * 16 + ph) << 32 | (hist & 0xFFFF)); ctx[k++] = H(31, (uint64_t)(ptype * 64 + min(sIdx, 3) * 8 + wb) << 32 | word); }
        if (D) { int pl = plen < 80 ? 0 : plen < 300 ? 1 : plen < 800 ? 2 : 3; ctx[k++] = H(32, (uint64_t)(turn * 16 + ptype * 4 + pl) << 32 | (hist & 0xFFFF)); ctx[k++] = H(33, (uint64_t)(turn * 4 + ptype) << 60 ^ ((uint64_t)w1 << 32 | word)); }
        if (Bv) { ctx[k++] = H(40, (uint64_t)lab[p] << 32 | (hist & 0xFFFFFF)); ctx[k++] = H(41, (uint64_t)lab[p] << 60 ^ ((uint64_t)w1 << 32 | word)); }
        if (C) { ctx[k++] = H(50, hist); ctx[k++] = H(51, (uint64_t)w2 << 32 | word); }
        int sel = !mixsel ? 0 : (Bv ? lab[p] : 0) * (A ? 4 : 1) + (A ? ptype : 0); sel %= NSEL;
        int c = d[i], c0 = 1;
        for (int b = 7; b >= 0; b--) {
            int y = (c >> b) & 1; float st[32], dot = 0; float *w = &W[(c0 * NSEL + sel) * (nin + 1)];
            for (int j = 0; j < nin; j++) { idx[j] = H(ctx[j], c0) >> (32 - TB); float pp = (T[j][idx[j]] >> 10) / 4194304.0f; pp = fminf(fmaxf(pp, 1e-4f), 1 - 1e-4f); st[j] = stretch(pp); dot += w[j] * st[j]; }
            st[nin] = 0.3f; dot += w[nin] * st[nin];
            float pr = fminf(fmaxf(squash(dot), 1e-5f), 1 - 1e-5f); bits += -log2(y ? pr : 1 - pr);
            float err = (y - pr) * 0.004f; for (int j = 0; j <= nin; j++) w[j] += err * st[j];
            for (int j = 0; j < nin; j++) { uint32_t v = T[j][idx[j]], cn = v & 1023, pv = v >> 10; int tg = y ? (1 << 22) - 1 : 0; pv += (int)((tg - (int)pv) / (cn + 1.6)); if (cn < 255) cn++; T[j][idx[j]] = pv << 10 | cn; }
            c0 = c0 * 2 + y; }
        { size_t ps = pstart[p]; size_t qe = d[ps] == '"' ? ps : ps + 2; if (ptype == 2 && i >= qe) { ptype = isq(d, ps) ? 1 : 0; if (ptype == 1) { turn = run & 1; run++; } else { run = 0; turn = 0; } } }
        curlen++;   // set only from characters already coded
        if (islet(c)) word = H(word, c | 32); else if (word) { w3 = w2; w2 = w1; w1 = word; word = 0; wIdx++; }
        if (c == '.' || c == '!' || c == '?') sIdx++;
        hist = hist << 8 | c;
    }
    printf("%-6s %-10s paragraphs %5d  text %9.0f  labels %6.0f  total %9.0f bytes\n", argv[1], argv[2], np, bits / 8, labbits / 8, (bits + labbits) / 8);
    // write the labels for the reorder test
    if (Bv) { string lf = string(argv[2]) + ".lab"; FILE *o = fopen(lf.c_str(), "w"); for (int p = 0; p < np; p++) fprintf(o, "%zu %d\n", pstart[p], lab[p]); fclose(o); }
    return 0;
}
