// ctxcm.cpp - small context-mixing text model for testing Michael Harrington's "context" ideas (10 Nov 2026).
// Michael Hugh Harrington. CC0 1.0 (public domain), see LICENSE.txt.
// Measures the model's code length (sum of -log2 p over every bit, the size an arithmetic coder would reach to within a
// few bytes). Same model for every variant; a variant only adds context inputs:
//   base  orders 1,2,3,4,6 + current word + word pair (7 inputs)
//   ctlN  base + N extra standard contexts (control: is any gain just from having more inputs?)
//   S     sentence position: words since the last . ? ! (fine count) and since the last ; : , with the mark type
//   Q     speech vs narration: inside a spoken quote or not, joined to order-3 and the word pair
//   SQ    both
// usage: ctxcm VARIANT FILE
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
using namespace std;
static const int TB = 22;
struct Slot { uint32_t v = 0x80000000u; };   // high 22 bits probability, low 10 bits count
static inline uint32_t H(uint64_t a, uint64_t b) { uint64_t x = (a + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull ^ (b + 0x94D049BB133111EBull); x ^= x >> 31; x *= 0xD6E8FEB86659FD93ull; x ^= x >> 29; return (uint32_t)x; }
static inline float stretch(float p) { return logf(p / (1 - p)); }
static inline float squash(float x) { return 1.0f / (1.0f + expf(-x)); }
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: ctxcm VARIANT FILE\n"); return 2; }
    string var = argv[1]; FILE *f = fopen(argv[2], "rb"); if (!f) return 1;
    vector<uint8_t> d; { int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); } fclose(f);
    string mix = ""; { size_t u = var.find('+'); if (u != string::npos) { mix = var.substr(u + 1); var = var.substr(0, u); } }
    bool S = var == "S" || var == "SQ", Q = var == "Q" || var == "SQ"; int ctl = var.rfind("ctl", 0) == 0 ? atoi(var.c_str() + 3) : 0;
    int nin = 7 + (S ? 2 : 0) + (Q ? 2 : 0) + ctl;
    vector<vector<Slot>> T(nin, vector<Slot>(1u << TB));
    const int NSEL = 64; vector<float> W(256 * NSEL * (nin + 1), 0.3f);
    vector<uint32_t> ctx(nin), idx(nin);
    uint64_t hist = 0; uint32_t word = 0, word1 = 0, word2 = 0;
    int wcSent = 0, wcSeg = 0, mark = 0; bool inQ = false; uint8_t prev = 0;
    double bits = 0;
    for (size_t i = 0; i < d.size(); i++) {
        // contexts for this byte (from bytes already seen)
        int k = 0;
        for (int o : {1, 2, 3, 4, 6}) ctx[k++] = H(o, hist & ((o == 8 ? ~0ull : (1ull << (8 * o)) - 1)));
        ctx[k++] = H(20, word); ctx[k++] = H(21, (uint64_t)word1 << 32 | word);
        if (S) { ctx[k++] = H(30, (uint64_t)min(wcSent, 63) << 40 | (uint64_t)mark << 32 | word);
                 ctx[k++] = H(31, (uint64_t)min(wcSeg, 31) << 40 | (uint64_t)min(wcSent, 63) << 32 | (hist & 0xFFFF)); }
        if (Q) { ctx[k++] = H(40, (uint64_t)inQ << 32 | (hist & 0xFFFFFF)); ctx[k++] = H(41, (uint64_t)inQ << 62 ^ ((uint64_t)word1 << 32 | word)); }
        static const uint64_t extra[] = {5, 7, 50, 51};
        for (int e = 0; e < ctl; e++) { uint64_t o = extra[e];
            if (o == 5 || o == 7) ctx[k++] = H(o, hist & ((1ull << (8 * o)) - 1));
            else if (o == 50) ctx[k++] = H(50, (uint64_t)word2 << 32 ^ (uint64_t)word1 << 16 ^ word);
            else ctx[k++] = H(51, hist & 0xFF00FF00ull); }   // sparse context
        int sel = 0;   // mixer weight set: "+s" sentence position, "+q" speech/narration, "+sq" both, "+c" control (previous byte's top bits)
        int wb = wcSent == 0 ? 0 : wcSent < 3 ? 1 : wcSent < 6 ? 2 : wcSent < 10 ? 3 : wcSent < 15 ? 4 : wcSent < 22 ? 5 : wcSent < 32 ? 6 : 7;
        if (mix == "s") sel = wb * 8 + mark; else if (mix == "q") sel = inQ; else if (mix == "sq") sel = (wb * 4 + min(mark, 3)) * 2 + inQ; else if (mix == "c") sel = (int)(hist & 0xFC) >> 2;
        int c = d[i], c0 = 1;
        for (int b = 7; b >= 0; b--) {
            int y = (c >> b) & 1; float st[64]; float dot = 0; float *w = &W[(c0 * NSEL + sel) * (nin + 1)];
            for (int j = 0; j < nin; j++) { idx[j] = (H(ctx[j], c0)) >> (32 - TB); float p = (T[j][idx[j]].v >> 10) / 4194304.0f; p = fminf(fmaxf(p, 1e-4f), 1 - 1e-4f); st[j] = stretch(p); dot += w[j] * st[j]; }
            st[nin] = 0.3f; dot += w[nin] * st[nin];
            float p = squash(dot); p = fminf(fmaxf(p, 1e-5f), 1 - 1e-5f);
            bits += -log2(y ? p : 1 - p);
            float err = (y - p) * 0.004f; for (int j = 0; j <= nin; j++) w[j] += err * st[j];
            for (int j = 0; j < nin; j++) { uint32_t v = T[j][idx[j]].v, n = v & 1023, pp = v >> 10; int target = y ? (1 << 22) - 1 : 0;
                pp += (int)((target - (int)pp) / (n + 1.6)); if (n < 255) n++; T[j][idx[j]].v = pp << 10 | n; }
            c0 = c0 * 2 + y;
        }
        // update text state after the byte
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (letter) word = H(word, c | 32);
        else if (word) { word2 = word1; word1 = word; word = 0; wcSent++; wcSeg++; }
        if (c == '.' || c == '?' || c == '!') { wcSent = 0; wcSeg = 0; mark = c == '.' ? 1 : c == '?' ? 2 : 3; }
        else if (c == ';' || c == ':' || c == ',') { wcSeg = 0; mark = c == ';' ? 4 : c == ':' ? 5 : 6; }
        else if (c == '\n' && prev == '\n') { wcSent = 0; wcSeg = 0; mark = 7; }
        bool pl = (prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z');
        if (c == '"') inQ = true; else if (c == '\'' && !pl) inQ = false; else if (c == '\n' && prev == '\n') inQ = false;
        hist = hist << 8 | c; prev = c;
    }
    printf("%s%s%s %s %zu -> %.0f bytes\n", var.c_str(), mix.empty() ? "" : "+", mix.c_str(), argv[2], d.size(), bits / 8);
    return 0;
}
