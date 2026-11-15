// stlx3 - geometry-mode STL mesh coder, C++ port (14 Nov 2026). Michael Hugh Harrington research, MIT License.
// Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.
// Each triangle is coded as "attached to recent side j" plus its third corner (new / one of 16 recent / farv).
// A new third corner is predicted as the parallelogram of the triangle across the shared side; other new corners from
// the corner before them. Corrections are stored as zigzag differences of order-keeping float bits, split into byte
// planes per axis. Keeps every triangle exactly (positions and winding); triangle order and rotation are free.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace stlx3 {
using std::vector;
static const int EF = 32, VF = 16;
static const uint8_t NEW = 48, SX_FAR = 49, RESTART = 50, RECENT = 51;

static inline uint32_t ford(uint32_t b) { return (b >> 31) ? ~b : (b | 0x80000000u); }
static inline uint32_t fbits(uint32_t o) { return (o >> 31) ? (o & 0x7fffffffu) : ~o; }
static inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t zz(uint32_t d) { int32_t s = (int32_t)d; return ((uint32_t)s << 1) ^ (uint32_t)(s >> 31); }
static inline uint32_t unzz(uint32_t z) { return (z >> 1) ^ (0u - (z & 1)); }
static inline uint64_t ek(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; }

struct State {
    uint64_t ef[EF]; int nef = 0; uint32_t vf[VF]; int nvf = 0;
    std::unordered_map<uint64_t, uint32_t> opp;
    int findE(uint64_t e) const { for (int i = 0; i < nef; i++) if (ef[i] == e) return i; return -1; }
    int findV(uint32_t v) const { for (int i = 0; i < nvf; i++) if (vf[i] == v) return i; return -1; }
    void frontE(uint64_t e) {
        int i = findE(e); if (i < 0) { i = nef < EF ? nef++ : EF - 1; }
        memmove(ef + 1, ef, i * sizeof(uint64_t)); ef[0] = e;
    }
    void frontV(uint32_t v) {
        int i = findV(v); if (i < 0) { i = nvf < VF ? nvf++ : VF - 1; }
        memmove(vf + 1, vf, i * sizeof(uint32_t)); vf[0] = v;
    }
    void push(uint32_t a, uint32_t b, uint32_t c) {
        frontE(ek(c, a)); frontE(ek(b, c)); frontE(ek(a, b));
        frontV(c); frontV(b); frontV(a);
        opp[ek(a, b)] = c; opp[ek(b, c)] = a; opp[ek(c, a)] = b;
    }
};

static void put32(vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 255); }
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void putv(vector<uint8_t>& o, uint32_t v) { while (v >= 128) { o.push_back((v & 127) | 128); v >>= 7; } o.push_back(v); }

// P: 3*nv floats, T: 3*nt vertex ids, already in the triangle order to code.
static vector<uint8_t> encode(const float* P, size_t nv_in, const uint32_t* T, size_t nt) {
    vector<int64_t> newid(nv_in, -1); vector<uint32_t> table; table.reserve(nv_in);
    vector<uint8_t> sym; sym.reserve(nt * 2); vector<uint32_t> farv;
    struct Src { uint8_t k; uint32_t a, b, c; }; vector<Src> src; src.reserve(nv_in);
    State S; S.opp.reserve(nt * 3); int64_t prev_new = -1;
    auto corner = [&](uint32_t g, const uint32_t* ids, int k, bool gate, uint32_t ga, uint32_t gb) -> uint32_t {
        if (newid[g] >= 0) {
            uint32_t i = (uint32_t)newid[g]; int q = S.findV(i);
            if (q >= 0) sym.push_back(RECENT + q); else { sym.push_back(SX_FAR); farv.push_back((uint32_t)table.size() - i); }
            return i;
        }
        uint32_t i = (uint32_t)table.size(); newid[g] = i; table.push_back(g); sym.push_back(NEW);
        auto it = gate ? S.opp.find(ek(gb, ga)) : S.opp.end();
        if (gate && it != S.opp.end()) src.push_back({1, ga, gb, it->second});
        else if (k > 0) src.push_back({0, ids[k - 1], 0, 0});
        else if (prev_new >= 0) src.push_back({0, (uint32_t)prev_new, 0, 0});
        else src.push_back({2, 0, 0, 0});
        prev_new = i; return i;
    };
    for (size_t t = 0; t < nt; t++) {
        uint32_t tri[3] = {T[3 * t], T[3 * t + 1], T[3 * t + 2]};
        int bj = -1, br = 0;
        for (int r = 0; r < 3; r++) {
            int64_t a = newid[tri[r]], b = newid[tri[(r + 1) % 3]];
            if (a >= 0 && b >= 0) { int j = S.findE(ek((uint32_t)b, (uint32_t)a)); if (j >= 0 && (bj < 0 || j < bj)) { bj = j; br = r; } }
        }
        if (bj >= 0) {
            uint32_t rt[3] = {tri[br], tri[(br + 1) % 3], tri[(br + 2) % 3]};
            uint32_t a = (uint32_t)newid[rt[0]], b = (uint32_t)newid[rt[1]]; uint32_t ids[2] = {a, b};
            sym.push_back((uint8_t)bj); uint32_t c = corner(rt[2], ids, 2, true, a, b); S.push(a, b, c);
        } else {
            sym.push_back(RESTART); uint32_t got[3];
            for (int k = 0; k < 3; k++) got[k] = corner(tri[k], got, k, false, 0, 0);
            S.push(got[0], got[1], got[2]);
        }
    }
    size_t nv = table.size(); vector<uint32_t> R(nv * 3);
    for (size_t i = 0; i < nv; i++) {
        const float* v = P + 3 * (size_t)table[i];
        for (int k = 0; k < 3; k++) {
            float p;
            const Src& s = src[i];
            if (s.k == 1) p = (P[3 * (size_t)table[s.a] + k] + P[3 * (size_t)table[s.b] + k]) - P[3 * (size_t)table[s.c] + k];
            else if (s.k == 0) p = P[3 * (size_t)table[s.a] + k];
            else p = 0.0f;
            R[3 * i + k] = zz(ford(f2u(v[k])) - ford(f2u(p)));
        }
    }
    vector<uint8_t> fv; for (uint32_t x : farv) putv(fv, x);
    vector<uint8_t> out; out.reserve(24 + sym.size() + fv.size() + nv * 12);
    out.insert(out.end(), {'S', 'X', '3', 0}); put32(out, (uint32_t)nv); put32(out, (uint32_t)nt); put32(out, (uint32_t)sym.size());
    put32(out, (uint32_t)farv.size()); put32(out, (uint32_t)fv.size());
    out.insert(out.end(), sym.begin(), sym.end()); out.insert(out.end(), fv.begin(), fv.end());
    size_t base = out.size(); out.resize(base + nv * 12);
    for (int k = 0; k < 3; k++) for (int j = 0; j < 4; j++) {
        uint8_t* dst = out.data() + base + (size_t)(k * 4 + j) * nv; int sh = 24 - 8 * j;
        for (size_t i = 0; i < nv; i++) dst[i] = (R[3 * i + k] >> sh) & 255;
    }
    return out;
}

static bool decode(const uint8_t* x, size_t n, vector<float>& V, vector<uint32_t>& F) {
    if (n < 24 || memcmp(x, "SX3", 4) != 0) return false;
    uint32_t nv = get32(x + 4), nt = get32(x + 8), ns = get32(x + 12), nfar = get32(x + 16), lf = get32(x + 20);
    const uint8_t* sym = x + 24; const uint8_t* fp = sym + ns; const uint8_t* rp = fp + lf;
    vector<uint32_t> farv(nfar); size_t q = 0;
    for (uint32_t i = 0; i < nfar; i++) { uint32_t v = 0; int s = 0; while (true) { uint8_t c = fp[q++]; v |= (uint32_t)(c & 127) << s; s += 7; if (c < 128) break; } farv[i] = v; }
    V.assign((size_t)nv * 3, 0.0f); F.resize((size_t)nt * 3);
    State S; S.opp.reserve((size_t)nt * 3); size_t si = 0, fi = 0; uint32_t nxt = 0; int64_t prev_new = -1;
    auto resid = [&](uint32_t i, int k) -> uint32_t {
        const uint8_t* b = rp + (size_t)(k * 4) * nv + i;
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[nv] << 16) | ((uint32_t)b[2 * (size_t)nv] << 8) | b[3 * (size_t)nv];
    };
    auto corner = [&](const uint32_t* ids, int k, bool gate, uint32_t ga, uint32_t gb) -> uint32_t {
        uint8_t s = sym[si++];
        if (s >= RECENT) return S.vf[s - RECENT];
        if (s == SX_FAR) return nxt - farv[fi++];
        uint32_t i = nxt++; float p[3];
        auto it = gate ? S.opp.find(ek(gb, ga)) : S.opp.end();
        for (int c = 0; c < 3; c++) {
            if (gate && it != S.opp.end()) p[c] = (V[3 * (size_t)ga + c] + V[3 * (size_t)gb + c]) - V[3 * (size_t)it->second + c];
            else if (k > 0) p[c] = V[3 * (size_t)ids[k - 1] + c];
            else if (prev_new >= 0) p[c] = V[3 * (size_t)prev_new + c];
            else p[c] = 0.0f;
            V[3 * (size_t)i + c] = u2f(fbits(ford(f2u(p[c])) + unzz(resid(i, c))));
        }
        prev_new = i; return i;
    };
    for (uint32_t t = 0; t < nt; t++) {
        uint8_t s = sym[si++];
        if (s < EF) {
            uint64_t e = S.ef[s]; uint32_t b = (uint32_t)(e >> 32), a = (uint32_t)e; uint32_t ids[2] = {a, b};
            uint32_t c = corner(ids, 2, true, a, b); S.push(a, b, c); F[3 * t] = a; F[3 * t + 1] = b; F[3 * t + 2] = c;
        } else {
            uint32_t got[3]; for (int k = 0; k < 3; k++) got[k] = corner(got, k, false, 0, 0);
            S.push(got[0], got[1], got[2]); memcpy(&F[3 * t], got, 12);
        }
    }
    return true;
}
}  // namespace stlx3
