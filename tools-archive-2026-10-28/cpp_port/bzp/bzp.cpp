// bzip2-style pipeline with three entropy stages, to test the ternary pointer
// idea on bzip2.  Front end follows bzip2 -9: initial run-length step (runs
// of 4..255 equal bytes become 4 bytes + a count), blocks of up to 899,981
// bytes, Burrows-Wheeler transform (sorted rotations), move-to-front.
// Entropy stage (bzip2 itself uses Huffman codes here):
//   mode 0 "plain"   : adaptive arithmetic coding of each MTF rank, order 0
//                      (8 yes/no decisions down a binary tree)
//   mode 1 "context" : control test - same, but the tree is chosen by the
//                      current zero-run length and the previous rank
//                      (the same information mode 2 gets)
//   mode 2 "pointer" : before each rank, one yes/no "is it the pointer?"
//                      (rank 0 = same symbol as the one before), chance from
//                      the plain model's own chance of rank 0 refined by a
//                      small mixer with run-length / recent-outcome tables;
//                      on "no" the rank is coded with rank 0 excluded.
// Everything is decoded again and compared with the input.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <algorithm>
typedef std::vector<uint8_t> V;

// ---- coder (same 32-bit binary arithmetic coder as HCM)
struct BitWriter { V d; uint32_t cur = 0; int nb = 0; void bit(int b) { cur = (cur << 1) | b; if (++nb == 8) { d.push_back((uint8_t)cur); cur = 0; nb = 0; } } void flush() { while (nb) bit(0); } };
struct BitReader { const uint8_t *d; uint64_t n, pos; int bit() { int b = 0; if (pos < n) b = (d[pos >> 3] >> (7 - (pos & 7))) & 1; pos++; return b; } };
static const uint64_t TOP = 0xFFFFFFFFULL, HALF = 1ULL << 31, Q1 = 1ULL << 30, Q3 = 3ULL << 30;
struct Coder {
    uint64_t lo = 0, hi = TOP, pend = 0, v = 0; BitWriter *w = 0; BitReader *r = 0;
    void out(int b) { w->bit(b); for (; pend; pend--) w->bit(1 - b); }
    void enc(int y, int p) {
        uint64_t rng = hi - lo + 1;
        if (y) hi = lo + ((rng * (uint64_t)p) >> 12) - 1; else lo = lo + ((rng * (uint64_t)p) >> 12);
        for (;;) {
            if (hi < HALF) out(0);
            else if (lo >= HALF) { out(1); lo -= HALF; hi -= HALF; }
            else if (lo >= Q1 && hi < Q3) { pend++; lo -= Q1; hi -= Q1; }
            else break;
            lo <<= 1; hi = (hi << 1) | 1;
        }
    }
    void finish() { pend++; out(lo < Q1 ? 0 : 1); }
    int dec(int p) {
        uint64_t rng = hi - lo + 1;
        int y = ((v - lo + 1) * 4096 - 1) < (uint64_t)p * rng;
        if (y) hi = lo + ((rng * (uint64_t)p) >> 12) - 1; else lo = lo + ((rng * (uint64_t)p) >> 12);
        for (;;) {
            if (hi < HALF) {}
            else if (lo >= HALF) { lo -= HALF; hi -= HALF; v -= HALF; }
            else if (lo >= Q1 && hi < Q3) { lo -= Q1; hi -= Q1; v -= Q1; }
            else break;
            lo <<= 1; hi = (hi << 1) | 1; v = (v << 1) | (uint64_t)r->bit();
        }
        return y;
    }
};

static int STI[4096], SQI[4096], RT15[256];
static void init_tables() {
    for (int p = 0; p < 4096; p++) { double q = (p ? p : 1) / 4096.0; int s = (int)lround(log(q / (1 - q)) * 256); STI[p] = s > 2047 ? 2047 : (s < -2047 ? -2047 : s); }
    for (int x = -2048; x < 2048; x++) { int p = (int)(4096.0 / (1 + exp(-x / 256.0))); SQI[x + 2048] = p < 1 ? 1 : (p > 4095 ? 4095 : p); }
    for (int n = 0; n < 256; n++) RT15[n] = (int)(32768.0 / (n + 1.6));
}
static inline int pr(uint32_t v) { int p = (int)(v & 0xFFFF) >> 4; return p < 1 ? 1 : (p > 4095 ? 4095 : p); }
static inline void upd(uint32_t *v, int y, int lim) {
    int pp = (int)(*v & 0xFFFF), nn = (int)(*v >> 16);
    pp += (((y ? 65535 : 0) - pp) * RT15[nn]) >> 15;
    nn = nn < lim ? nn + 1 : nn;
    *v = (uint32_t)pp | ((uint32_t)nn << 16);
}
static int LIM = 30;

// ---- entropy stage over one block of MTF ranks
template <int DEC>
static void code_ranks(int mode, uint8_t *rk, size_t n, Coder *cd) {
    static uint32_t T[12][256];
    for (int k = 0; k < 12; k++) for (int i = 0; i < 256; i++) T[k][i] = 0x8000;
    uint32_t EP[64], EP2[64];
    int32_t EW[64][4];
    for (int i = 0; i < 64; i++) { EP[i] = EP2[i] = 0x8000; EW[i][0] = 65536; EW[i][1] = EW[i][2] = EW[i][3] = 0; }
    int run = 0, prev = 0, fh = 0;
    for (size_t i = 0; i < n; i++) {
        int r = DEC ? 0 : rk[i];
        int tsel = 0;
        if (mode == 1) tsel = (run < 3 ? run : 3) * 3 + (prev < 2 ? prev : 2);
        uint32_t *t = T[tsel];
        int excl = 0;
        if (mode == 2) {
            // model's own chance of rank 0: product along the all-zero path
            uint64_t P = 1ULL << 30; int q[8];
            uint32_t c0 = 1;
            for (int b = 7; b >= 0; b--) { q[b] = 4096 - pr(t[c0]); P = (P * (uint64_t)q[b]) >> 12; c0 <<= 1; }
            int P12 = (int)(P >> 18); P12 = P12 < 1 ? 1 : (P12 > 4095 ? 4095 : P12);
            int ectx = (run < 15 ? run : 15) * 4 + fh;
            int e2 = (prev < 15 ? prev : 15) * 4 + fh;
            int xe[4] = {STI[P12], STI[pr(EP[ectx])], 256, STI[pr(EP2[e2])]};
            int64_t d = 0; for (int k = 0; k < 4; k++) d += (int64_t)EW[ectx][k] * xe[k];
            int s = (int)(d >> 16); s = s > 2047 ? 2047 : (s < -2047 ? -2047 : s);
            int pe = SQI[s + 2048];
            int e;
            if (DEC) e = cd->dec(pe); else { e = r == 0; cd->enc(e, pe); }
            int err = (e << 12) - pe;
            for (int k = 0; k < 4; k++) EW[ectx][k] += (xe[k] * err) >> 11;
            upd(&EP[ectx], e, 255); upd(&EP2[e2], e, 255);
            fh = ((fh << 1) | e) & 3;
            if (e) {   // learn rank 0's bits in the tree, nothing coded
                uint32_t c = 1;
                for (int b = 7; b >= 0; b--) { upd(&t[c], 0, LIM); c <<= 1; }
                r = 0; goto done;
            }
            excl = 1;
            // remaining chance of finishing on rank 0 below each bit
            uint32_t R[8]; R[0] = 65536;
            for (int k = 1; k < 8; k++) R[k] = (uint32_t)(((uint64_t)R[k - 1] * q[k - 1]) >> 12);
            uint32_t c = 1; int onp = 1;
            for (int b = 7; b >= 0; b--) {
                int p = pr(t[c]), y, forced = -1;
                if (onp) {   // still on rank 0's path: remove its share
                    if (R[b] >= 65536) forced = 1;
                    else {
                        uint64_t pb = (uint64_t)(4096 - p);
                        int pb2 = (int)((pb * (65536 - R[b]) * 4096) / ((uint64_t)4096 * 65536 - pb * R[b]));
                        pb2 = pb2 < 1 ? 1 : (pb2 > 4095 ? 4095 : pb2);
                        p = 4096 - pb2;
                    }
                }
                if (forced >= 0) y = forced;
                else if (DEC) y = cd->dec(p);
                else { y = (r >> b) & 1; cd->enc(y, p); }
                upd(&t[c], y, LIM);
                if (y) onp = 0;
                c = (c << 1) | y;
            }
            r = c & 255;
            goto done;
        }
        {
            (void)excl;
            uint32_t c = 1;
            for (int b = 7; b >= 0; b--) {
                int p = pr(t[c]), y;
                if (DEC) y = cd->dec(p); else { y = (r >> b) & 1; cd->enc(y, p); }
                upd(&t[c], y, LIM);
                c = (c << 1) | y;
            }
            r = c & 255;
        }
    done:
        if (DEC) rk[i] = (uint8_t)r;
        run = r == 0 ? run + 1 : 0;
        prev = r;
    }
}

// ---- bzip2 front end
static V rle1_enc(const V &in) {
    V o; size_t i = 0;
    while (i < in.size()) {
        size_t j = i; while (j < in.size() && in[j] == in[i] && j - i < 255) j++;
        size_t r = j - i;
        if (r >= 4) { for (int k = 0; k < 4; k++) o.push_back(in[i]); o.push_back((uint8_t)(r - 4)); }
        else for (size_t k = 0; k < r; k++) o.push_back(in[i]);
        i = j;
    }
    return o;
}
static V rle1_dec(const V &in) {
    V o; size_t i = 0;
    while (i < in.size()) {
        int c = in[i]; size_t j = i; while (j < in.size() && in[j] == c && j - i < 4) j++;
        size_t r = j - i;
        for (size_t k = 0; k < r; k++) o.push_back((uint8_t)c);
        i = j;
        if (r == 4) { int cnt = in[i++]; for (int k = 0; k < cnt; k++) o.push_back((uint8_t)c); }
    }
    return o;
}
// BWT by sorting rotations (prefix doubling on cyclic ranks)
static void bwt(const uint8_t *s, int n, uint8_t *L, int *primary) {
    std::vector<int> sa(n), rk(n), tmp(n);
    for (int i = 0; i < n; i++) { sa[i] = i; rk[i] = s[i]; }
    for (int k = 1;; k <<= 1) {
        auto key2 = [&](int i) { return rk[(i + k) % n]; };
        std::sort(sa.begin(), sa.end(), [&](int a, int b) { return rk[a] != rk[b] ? rk[a] < rk[b] : key2(a) < key2(b); });
        tmp[sa[0]] = 0;
        for (int i = 1; i < n; i++) tmp[sa[i]] = tmp[sa[i - 1]] + (rk[sa[i]] != rk[sa[i - 1]] || key2(sa[i]) != key2(sa[i - 1]));
        rk = tmp;
        if (rk[sa[n - 1]] == n - 1 || k >= n) break;
    }
    for (int i = 0; i < n; i++) { L[i] = s[(sa[i] + n - 1) % n]; if (sa[i] == 0) *primary = i; }
}
static void ibwt(const uint8_t *L, int n, int primary, uint8_t *s) {
    std::vector<int> cnt(256, 0), T(n);
    for (int i = 0; i < n; i++) cnt[L[i]]++;
    int sum = 0; for (int c = 0; c < 256; c++) { int t = cnt[c]; cnt[c] = sum; sum += t; }
    for (int i = 0; i < n; i++) T[cnt[L[i]]++] = i;
    int p = T[primary];
    for (int i = 0; i < n; i++) { s[i] = L[p]; p = T[p]; }
}
static void mtf(uint8_t *d, int n, int inv) {
    uint8_t lst[256]; for (int i = 0; i < 256; i++) lst[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {
        int r;
        if (!inv) { int c = d[i]; r = 0; while (lst[r] != c) r++; d[i] = (uint8_t)r; }
        else r = d[i];
        int c = lst[r]; memmove(lst + 1, lst, r); lst[0] = (uint8_t)c;
        if (inv) d[i] = (uint8_t)c;
    }
}
static double cpu() { return (double)clock() / CLOCKS_PER_SEC; }
static const int BLOCK = 899981;

int main(int argc, char **argv) {
    init_tables();
    if (getenv("LIM")) LIM = atoi(getenv("LIM"));
    printf("%-13s %8s %9s %9s %9s   %s\n", "file", "size", "plain", "context", "pointer", "entropy-stage time c/d (plain, context, pointer)");
    double backend = 0; uint64_t tot[3] = {0, 0, 0}, tin = 0; double tt[3][2] = {{0}}; int allok = 1;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); V in; int ch; while ((ch = fgetc(f)) != EOF) in.push_back((uint8_t)ch); fclose(f);
        V r1 = rle1_enc(in);
        // front end once (shared by all three modes)
        std::vector<V> blocks; std::vector<int> prim;
        for (size_t off = 0; off < r1.size(); off += BLOCK) {
            int n = (int)std::min((size_t)BLOCK, r1.size() - off);
            V L(n); int p; bwt(r1.data() + off, n, L.data(), &p);
            mtf(L.data(), n, 0);
            blocks.push_back(L); prim.push_back(p);
        }
        size_t out[3]; double tc[3], td[3];
        for (int mode = 0; mode < 3; mode++) {
            std::vector<V> streams;
            double t0 = cpu();
            for (auto &bl : blocks) { BitWriter w; Coder e; e.w = &w; V cp = bl; code_ranks<0>(mode, cp.data(), cp.size(), &e); e.finish(); w.flush(); streams.push_back(w.d); }
            double t1 = cpu();
            out[mode] = 4;   // original length
            for (auto &st : streams) out[mode] += st.size() + 12;   // per block: size, primary index, stream length
            V back;
            double dt = 0;
            for (size_t k = 0; k < blocks.size(); k++) {
                double u0 = cpu();
                BitReader rd = {streams[k].data(), (uint64_t)streams[k].size() * 8, 0};
                Coder d; d.r = &rd;
                for (int i = 0; i < 32; i++) d.v = (d.v << 1) | (uint64_t)rd.bit();
                V rk(blocks[k].size());
                code_ranks<1>(mode, rk.data(), rk.size(), &d);
                dt += cpu() - u0;
                if (rk != blocks[k]) allok = 0;
                double u1 = cpu();
                mtf(rk.data(), (int)rk.size(), 1);
                V s2(rk.size()); ibwt(rk.data(), (int)rk.size(), prim[k], s2.data());
                back.insert(back.end(), s2.begin(), s2.end());
                if (mode == 0) backend += cpu() - u1;
            }
            double u2 = cpu();
            if (rle1_dec(back) != in) allok = 0;
            if (mode == 0) backend += cpu() - u2;
            tc[mode] = t1 - t0; td[mode] = dt;
        }
        const char *b = strrchr(argv[a], '/'); b = b ? b + 1 : argv[a];
        printf("%-13s %8zu %9zu %9zu %9zu   %.3f/%.3f  %.3f/%.3f  %.3f/%.3f  blocks %zu\n", b, in.size(), out[0], out[1], out[2], tc[0], td[0], tc[1], td[1], tc[2], td[2], blocks.size());
        for (int m = 0; m < 3; m++) { tot[m] += out[m]; tt[m][0] += tc[m]; tt[m][1] += td[m]; }
        tin += in.size();
    }
    printf("TOTAL %llu  plain %llu (%.0f per million)  context %llu (%.0f)  pointer %llu (%.0f)  all-ok %d\n", (unsigned long long)tin,
           (unsigned long long)tot[0], tot[0] * 1e6 / tin, (unsigned long long)tot[1], tot[1] * 1e6 / tin, (unsigned long long)tot[2], tot[2] * 1e6 / tin, allok);
    printf("decode back end (inverse MTF + inverse BWT + run-length), once: %.2f s\n", backend);
    printf("entropy-stage CPU: plain %.2f/%.2f  context %.2f/%.2f  pointer %.2f/%.2f\n", tt[0][0], tt[0][1], tt[1][0], tt[1][1], tt[2][0], tt[2][1]);
    return 0;
}
