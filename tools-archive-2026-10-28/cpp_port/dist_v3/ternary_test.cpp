// Ternary experiment: one context-mixing engine run on different
// representations of the same file. Reports ideal code length (sum of
// -log2 p over all coded decisions) in bytes, plus 1,000,000-bit figures.
//
// Representations:
//   bytes   : alphabet 256, 8 binary decisions per symbol, contexts = previous 1,2,3,4,6,8 bytes
//   tern6   : conventional ternary, each byte -> 6 base-3 digits (most significant first);
//             contexts = previous 1,2,3,4,6,8 whole bytes + position in byte + digits so far
//   habits  : HaBiTS parse of the bit stream (0 -> 2, 10 -> 0, 11 -> 1); no byte alignment;
//             contexts = previous 6,12,18,24,36,48 trits
//   bits    : the plain bit stream; contexts = previous 8,16,24,32,48,64 bits
// A trit is coded as up to two binary decisions: "is it 0?", then "is it 1 (vs 2)?".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vector>

static const int TB = 22;
static const uint32_t TM = (1u << TB) - 1;
static double STR[4096];
static int SQ(double x) { double p = 4096.0 / (1.0 + exp(-x)); int q = (int)p; return q < 1 ? 1 : (q > 4095 ? 4095 : q); }

struct Model {
    enum { NC = 6, NIN = NC + 2 };
    std::vector<uint16_t> P[NC + 1];
    std::vector<uint8_t> N[NC + 1];
    std::vector<double> W;      // weights per node-select
    std::vector<uint16_t> A;    // APM 33 bins per row
    int nsel, arows;
    double bits = 0;
    Model(int nsel_, int arows_) : nsel(nsel_), arows(arows_) {
        for (int i = 0; i <= NC; i++) { P[i].assign((size_t)1 << TB, 32768); N[i].assign((size_t)1 << TB, 0); }
        W.assign((size_t)nsel * NIN, 0.3);
        A.resize((size_t)arows * 33);
        for (int r = 0; r < arows; r++) for (int j = 0; j < 33; j++) A[(size_t)r * 33 + j] = (uint16_t)(SQ((j - 16) / 2.0) * 16);
    }
    // code one binary decision y with context hashes h[0..NC-1], order-0 index o0, mixer select sel, apm row ar
    void code(int y, const uint32_t *h, uint32_t o0, int sel, int ar) {
        uint32_t idx[NC + 1];
        double st[NIN];
        for (int i = 0; i < NC; i++) { idx[i] = h[i] & TM; st[i] = STR[P[i][idx[i]] >> 4]; }
        idx[NC] = o0 & TM; st[NC] = STR[P[NC][idx[NC]] >> 4];
        st[NC + 1] = 0.25;
        double *w = &W[(size_t)sel * NIN];
        double dot = 0; for (int i = 0; i < NIN; i++) dot += w[i] * st[i];
        double pm = 4096.0 / (1.0 + exp(-(dot > 20 ? 20 : (dot < -20 ? -20 : dot))));
        double s = dot < 7.999 ? dot : 7.999; s = s > -7.999 ? s : -7.999;
        double pos = (s + 8) * 2; int j = (int)pos; double f = pos - j;
        uint16_t *row = &A[(size_t)ar * 33];
        double pa = (row[j] * (1 - f) + row[j + 1] * f) / 16.0;
        double p = pm * 0.1 + pa * 0.9;
        if (p < 1) p = 1; if (p > 4095) p = 4095;
        double q = p / 4096.0;
        bits += -log2(y ? q : 1 - q);
        double err = (y - pm / 4096.0) * 0.008;
        for (int i = 0; i < NIN; i++) w[i] += err * st[i];
        int tgt = y ? 65535 : 0;
        for (int i = 0; i <= NC; i++) {
            int pp = P[i][idx[i]], n = N[i][idx[i]];
            pp += (int)(((int64_t)(tgt - pp) * (int)(65536.0 / (n + 1.6))) >> 16);
            P[i][idx[i]] = (uint16_t)pp;
            if (n < 255) N[i][idx[i]] = (uint8_t)(n + 1);
        }
        int t16 = y ? 65535 : 0;
        row[j] = (uint16_t)(row[j] + (int)((t16 - row[j]) * (1 - f)) / 64);
        row[j + 1] = (uint16_t)(row[j + 1] + (int)((t16 - row[j + 1]) * f) / 64);
    }
};

static inline uint32_t hmix(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }

// hash of the last k symbols of seq ending before position pos, salted
static uint32_t hist_hash(const std::vector<uint8_t> &seq, size_t pos, int k, uint32_t salt) {
    uint32_t x = salt * 0x9E3779B1u + (uint32_t)k;
    for (int j = 1; j <= k; j++) { uint32_t c = pos >= (size_t)j ? seq[pos - j] : 255; x = (x ^ c) * 0x01000193u + (uint32_t)j; }
    return hmix(x);
}

static double run_bytes(const std::vector<uint8_t> &d) {
    Model m(256, 65536);
    const int ord[6] = {1, 2, 3, 4, 6, 8};
    uint32_t hb[6], h[6];
    for (size_t i = 0; i < d.size(); i++) {
        for (int k = 0; k < 6; k++) hb[k] = hist_hash(d, i, ord[k], k + 1);
        int last = i ? d[i - 1] : 0;
        uint32_t c0 = 1;
        for (int b = 7; b >= 0; b--) {
            int y = (d[i] >> b) & 1;
            for (int k = 0; k < 6; k++) h[k] = hmix(hb[k] + c0 * 0x2545F491u);
            m.code(y, h, c0, (int)c0, (int)(c0 + 256 * last));
            c0 = (c0 << 1) | y;
        }
    }
    return m.bits;
}

// conventional ternary: 6 base-3 digits per byte; each trit = up to 2 decisions
static double run_tern6(const std::vector<uint8_t> &d) {
    Model m(6 * 243 * 2, 6 * 243 * 2 * 256 / 64);
    const int ord[6] = {1, 2, 3, 4, 6, 8};
    uint32_t hb[6], h[6];
    for (size_t i = 0; i < d.size(); i++) {
        for (int k = 0; k < 6; k++) hb[k] = hist_hash(d, i, ord[k], k + 11);
        int last = i ? d[i - 1] : 0;
        int v = d[i], dig[6];
        for (int t = 5; t >= 0; t--) { dig[t] = v % 3; v /= 3; }
        uint32_t pre = 1;   // digits so far, in base 3 with a leading 1
        for (int t = 0; t < 6; t++) {
            int tr = dig[t];
            for (int node = 0; node < 2; node++) {
                if (node == 1 && tr == 0) break;
                int y = node == 0 ? (tr == 0) : (tr == 1);
                uint32_t nctx = pre * 2 + (uint32_t)node;
                for (int k = 0; k < 6; k++) h[k] = hmix(hb[k] + nctx * 0x2545F491u);
                int sel = (int)((t * 243 + (pre % 243)) * 2 + node);
                m.code(y, h, nctx + 7777, sel, (int)((sel % (6 * 243 * 2)) * 256 / 64 + (last >> 6)) % (6 * 243 * 2 * 256 / 64));
            }
            pre = pre * 3 + (uint32_t)tr;
        }
    }
    return m.bits;
}

// generic small-alphabet stream (bits: A=2, trits: A=3); contexts = previous k symbols
static double run_stream(const std::vector<uint8_t> &s, int A, const int *ord) {
    Model m(2 * 64, 2 * 64 * 16);
    uint32_t h[6];
    for (size_t i = 0; i < s.size(); i++) {
        int sym = s[i];
        int prev = i ? s[i - 1] : 0;
        int prev2 = i > 1 ? s[i - 2] : 0;
        uint32_t hb[6];
        for (int k = 0; k < 6; k++) hb[k] = hist_hash(s, i, ord[k], k + 21);
        int pos6 = (int)(i % 64);
        for (int node = 0; node < (A == 3 ? 2 : 1); node++) {
            if (node == 1 && sym == 0) break;
            int y = A == 3 ? (node == 0 ? (sym == 0) : (sym == 1)) : sym;
            for (int k = 0; k < 6; k++) h[k] = hmix(hb[k] + (uint32_t)node * 0x2545F491u);
            int sel = node * 64 + (prev * 4 + prev2) % 64;
            m.code(y, h, (uint32_t)(node * 9 + prev * 3 + prev2), sel, sel * 16 + (pos6 & 15));
        }
    }
    return m.bits;
}

int main(int argc, char **argv) {
    for (int p = 1; p < 4096; p++) STR[p] = log((double)p / (4096 - p));
    STR[0] = STR[1];
    double T[4] = {0, 0, 0, 0}; double tin = 0;
    printf("%-13s %10s %10s %10s %10s   (1,000,000 bits down to)\n", "file", "bytes", "tern6", "habits", "bits");
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); std::vector<uint8_t> d; int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); fclose(f);
        // bit stream
        std::vector<uint8_t> bits; for (uint8_t b : d) for (int k = 7; k >= 0; k--) bits.push_back((b >> k) & 1);
        // HaBiTS parse: 0 -> 2, 10 -> 0, 11 -> 1 (a lone trailing 1 is padded with a 0)
        std::vector<uint8_t> tr;
        for (size_t i = 0; i < bits.size();) {
            if (bits[i] == 0) { tr.push_back(2); i++; }
            else { int nx = i + 1 < bits.size() ? bits[i + 1] : 0; tr.push_back(nx ? 1 : 0); i += 2; }
        }
        const int ob[6] = {8, 16, 24, 32, 48, 64};
        const int ot[6] = {6, 12, 18, 24, 36, 48};
        double r[4];
        r[0] = run_bytes(d); r[1] = run_tern6(d); r[2] = run_stream(tr, 3, ot); r[3] = run_stream(bits, 2, ob);
        const char *base = strrchr(argv[a], '/'); base = base ? base + 1 : argv[a];
        double nb = d.size() * 8.0;
        printf("%-13s", base);
        for (int k = 0; k < 4; k++) { printf(" %10.0f", r[k] / nb * 1e6); T[k] += r[k]; }
        printf("   (habits trits: %zu for %zu bytes)\n", tr.size(), d.size());
        tin += nb;
    }
    printf("%-13s", "ALL");
    for (int k = 0; k < 4; k++) printf(" %10.0f", T[k] / tin * 1e6);
    printf("\n");
    return 0;
}
