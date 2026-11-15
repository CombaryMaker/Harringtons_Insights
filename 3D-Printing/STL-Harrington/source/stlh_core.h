// STL-Harrington - core (first prototype, 15 Nov 2026). Michael Hugh Harrington, MIT License.
// Created with the assistance of Claude AI (Anthropic); direction, ideas and structure by Michael Hugh Harrington.
//
// Compress: read an STL (binary or ASCII) or OBJ file, code the mesh with the stlx3 geometry coder (stlx3.h), choose the
// best of three triangle orders, pack with ZPAQ (smallest) or zstd (fast), then decode the result again and check it.
// Restore: unpack a .stlh file back into a binary STL file with the same triangles and bit-exact corner coordinates.
//
// .stlh file layout (all numbers little-endian):
//   "STLH"            4 bytes, magic
//   version           1 byte  (1)
//   packer            1 byte  (1 = ZPAQ method 5, 2 = zstd level 19)
//   flags             1 byte  (bit 0: the original was a binary STL and its 80-byte header is kept)
//   reserved          1 byte  (0)
//   inner size        4 bytes, bytes of the unpacked inner block
//   inner CRC-32      4 bytes, CRC-32 of the unpacked inner block
//   packed data       the rest: the inner block packed by the packer
// Inner block: [80-byte original STL header, only if flag bit 0] + stlx3 block ("SX3\0" ...).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <charconv>
#include "stlx3.h"
#include "meshoptimizer.h"
#include "zstd.h"
#include "libzpaq.h"

namespace stlh {
using std::string; using std::vector; typedef vector<uint8_t> Bytes;

struct Mesh { vector<float> P; vector<uint32_t> F; size_t nv() const { return P.size() / 3; } size_t nt() const { return F.size() / 3; } };
struct Result { bool ok = false; string error; size_t inBytes = 0, outBytes = 0, triangles = 0, vertices = 0; bool verified = false; int order = 0; };
enum Packer { ZPAQ = 1, ZSTD = 2 };

// ---- CRC-32 ------------------------------------------------------------------------------------------------------
static uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t t[256]; static bool made = false;
    if (!made) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } made = true; }
    uint32_t c = 0xffffffffu; while (n--) c = t[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
}

// ---- packers -----------------------------------------------------------------------------------------------------
struct ZR : libzpaq::Reader { const Bytes& b; size_t p = 0; ZR(const Bytes& x) : b(x) {} int get() override { return p < b.size() ? b[p++] : -1; } };
struct ZW : libzpaq::Writer { Bytes b; void put(int c) override { b.push_back((uint8_t)c); } };
static Bytes zstd_pack(const Bytes& in, int level) { Bytes o(ZSTD_compressBound(in.size())); size_t r = ZSTD_compress(o.data(), o.size(), in.data(), in.size(), level); if (ZSTD_isError(r)) return Bytes(); o.resize(r); return o; }
static bool pack(int packer, const Bytes& in, Bytes& out) {
    if (packer == ZSTD) { out = zstd_pack(in, 19); return !out.empty() || in.empty(); }
    ZR r(in); ZW w; libzpaq::compress(&r, &w, "5"); out.swap(w.b); return true;
}
static bool unpack(int packer, const uint8_t* p, size_t n, size_t outSize, Bytes& out) {
    if (packer == ZSTD) { out.resize(outSize); size_t r = ZSTD_decompress(out.data(), outSize, p, n); return !ZSTD_isError(r) && r == outSize; }
    if (packer == ZPAQ) { Bytes in(p, p + n); ZR r(in); ZW w; libzpaq::decompress(&r, &w); out.swap(w.b); return out.size() == outSize; }
    return false;
}

// ---- reading meshes (every triangle is kept, including flat ones; STL allows them) --------------------------------
struct K3 { uint32_t a, b, c; bool operator==(const K3& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KH { size_t operator()(const K3& k) const { uint64_t h = k.a * 0x9E3779B97F4A7C15ull ^ (k.b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^ k.c * 0x94D049BB133111EBull; return (size_t)(h ^ (h >> 31)); } };
static void add_tri(Mesh& m, std::unordered_map<K3, uint32_t, KH>& id, const float* v9) {
    for (int k = 0; k < 3; k++) {
        K3 key; memcpy(&key, v9 + 3 * k, 12); auto it = id.find(key); uint32_t ix;
        if (it == id.end()) { ix = (uint32_t)m.nv(); id.emplace(key, ix); m.P.insert(m.P.end(), v9 + 3 * k, v9 + 3 * k + 3); }
        else ix = it->second;
        m.F.push_back(ix);
    }
}
static bool read_float(const char*& p, const char* e, float& out) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    char b[72]; int n = 0;
    while (p < e && n < 70 && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) b[n++] = *p++;
    if (n == 0 || n >= 70) return false; b[n] = 0; char* end = nullptr; out = strtof(b, &end); return end == b + n;
}
static bool is_binary_stl(const Bytes& b) { if (b.size() < 84) return false; uint32_t n; memcpy(&n, &b[80], 4); return (uint64_t)84 + 50ull * n == b.size(); }
static bool load_stl(const Bytes& b, Mesh& m, string& err) {
    std::unordered_map<K3, uint32_t, KH> id;
    if (is_binary_stl(b)) {
        uint32_t n; memcpy(&n, &b[80], 4); id.reserve((size_t)n * 2); m.F.reserve((size_t)n * 3);
        for (size_t t = 0; t < n; t++) { float v[9]; memcpy(v, &b[84 + 50 * t + 12], 36); add_tri(m, id, v); }
        if (n == 0) { err = "the STL file has no triangles"; return false; }
        return true;
    }
    const char* p = (const char*)b.data(); const char* e = p + b.size(); float v[9]; int k = 0;
    while (p < e) {
        const char* q = (const char*)memchr(p, 'v', e - p); if (!q) break;
        if (e - q >= 6 && memcmp(q, "vertex", 6) == 0 && (q == (const char*)b.data() || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r')) {
            p = q + 6; float x, y, z;
            if (!read_float(p, e, x) || !read_float(p, e, y) || !read_float(p, e, z)) { err = "unreadable number in ASCII STL"; return false; }
            v[3 * k] = x; v[3 * k + 1] = y; v[3 * k + 2] = z; if (++k == 3) { add_tri(m, id, v); k = 0; }
        } else p = q + 1;
    }
    if (m.nt() == 0) { err = "no triangles found (not an STL file?)"; return false; }
    return true;
}
static bool load_obj(const Bytes& b, Mesh& m, string& err) {
    std::unordered_map<K3, uint32_t, KH> id; vector<float> V; const char* p = (const char*)b.data(); const char* e = p + b.size();
    while (p < e) {
        const char* nl = (const char*)memchr(p, '\n', e - p); const char* le = nl ? nl : e;
        while (p < le && (*p == ' ' || *p == '\t')) p++;
        if (le - p > 2 && p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2; float x, y, z; if (!read_float(p, le, x) || !read_float(p, le, y) || !read_float(p, le, z)) { err = "unreadable vertex in OBJ"; return false; }
            V.push_back(x); V.push_back(y); V.push_back(z);
        } else if (le - p > 2 && p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2; vector<long> idx;
            while (p < le) {
                while (p < le && (*p == ' ' || *p == '\t' || *p == '\r')) p++; if (p >= le) break;
                long i = 0; auto r = std::from_chars(p, le, i); if (r.ec != std::errc()) { err = "unreadable face in OBJ"; return false; }
                p = r.ptr; while (p < le && *p != ' ' && *p != '\t' && *p != '\r') p++;
                long n = (long)(V.size() / 3); long j = i < 0 ? n + i : i - 1; if (j < 0 || j >= n) { err = "face points to a missing vertex"; return false; }
                idx.push_back(j);
            }
            for (size_t t = 1; t + 1 < idx.size(); t++) { float v9[9]; long c[3] = {idx[0], idx[t], idx[t + 1]}; for (int k = 0; k < 3; k++) memcpy(v9 + 3 * k, &V[3 * c[k]], 12); add_tri(m, id, v9); }
        }
        p = le + 1;
    }
    if (m.nt() == 0) { err = "no faces found in OBJ"; return false; }
    return true;
}

// ---- triangle orders --------------------------------------------------------------------------------------------
static vector<uint32_t> walk_order(const Mesh& m) {   // depth-first walk across shared sides
    size_t nt = m.nt(); std::unordered_map<uint64_t, vector<uint32_t>> edge; edge.reserve(nt * 2);
    for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) { uint32_t u = m.F[3 * t + k], v = m.F[3 * t + (k + 1) % 3]; edge[stlx3::ek(std::min(u, v), std::max(u, v))].push_back((uint32_t)t); }
    vector<char> seen(nt, 0); vector<uint32_t> out; out.reserve(nt * 3); vector<uint32_t> st;
    for (size_t t0 = 0; t0 < nt; t0++) {
        if (seen[t0]) continue; seen[t0] = 1; st.push_back((uint32_t)t0);
        while (!st.empty()) {
            uint32_t t = st.back(); st.pop_back(); out.insert(out.end(), &m.F[3 * t], &m.F[3 * t] + 3);
            uint32_t a = m.F[3 * t], b = m.F[3 * t + 1], c = m.F[3 * t + 2]; uint32_t sides[3][2] = {{c, a}, {b, c}, {a, b}};
            for (auto& s : sides) for (uint32_t n : edge[stlx3::ek(std::min(s[0], s[1]), std::max(s[0], s[1]))]) if (!seen[n]) { seen[n] = 1; st.push_back(n); }
        }
    }
    return out;
}

// ---- exactness check: same triangles (any order, any rotation), bit-exact corners, same winding --------------------
typedef std::array<uint32_t, 9> Tri;
static vector<Tri> canon(const vector<float>& P, const vector<uint32_t>& F) {
    vector<Tri> out(F.size() / 3);
    for (size_t t = 0; t < out.size(); t++) {
        Tri best{}; for (int r = 0; r < 3; r++) {
            Tri c; for (int j = 0; j < 3; j++) memcpy(&c[3 * j], &P[3 * (size_t)F[3 * t + (r + j) % 3]], 12);
            if (r == 0 || c < best) best = c;
        }
        out[t] = best;
    }
    std::sort(out.begin(), out.end()); return out;
}

// ---- writing a binary STL ------------------------------------------------------------------------------------------
static Bytes write_stl(const vector<float>& P, const vector<uint32_t>& F, const uint8_t* header80) {
    size_t nt = F.size() / 3; Bytes o(84 + 50 * nt, 0);
    if (header80) memcpy(o.data(), header80, 80); else { const char* h = "binary STL restored by STL-Harrington"; memcpy(o.data(), h, strlen(h)); }
    uint32_t n = (uint32_t)nt; memcpy(&o[80], &n, 4);
    for (size_t t = 0; t < nt; t++) {
        const float* a = &P[3 * (size_t)F[3 * t]]; const float* b = &P[3 * (size_t)F[3 * t + 1]]; const float* c = &P[3 * (size_t)F[3 * t + 2]];
        float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2], nz = u[0] * v[1] - u[1] * v[0];
        float len = std::sqrt(nx * nx + ny * ny + nz * nz); float nn[3] = {0, 0, 0}; if (len > 0) { nn[0] = nx / len; nn[1] = ny / len; nn[2] = nz / len; }
        uint8_t* r = &o[84 + 50 * t]; memcpy(r, nn, 12); memcpy(r + 12, a, 12); memcpy(r + 24, b, 12); memcpy(r + 36, c, 12);
    }
    return o;
}

// ---- restore -----------------------------------------------------------------------------------------------------
static bool is_stlh(const Bytes& b) { return b.size() >= 16 && memcmp(b.data(), "STLH", 4) == 0; }
static bool unpack_mesh(const Bytes& b, Mesh& m, Bytes& header, string& err) {
    if (!is_stlh(b)) { err = "not an STL-Harrington file"; return false; }
    if (b[4] != 1) { err = "made by a newer version of STL-Harrington"; return false; }
    uint32_t n, crc; memcpy(&n, &b[8], 4); memcpy(&crc, &b[12], 4);
    Bytes inner; if (!unpack(b[5], b.data() + 16, b.size() - 16, n, inner)) { err = "the packed data is damaged"; return false; }
    if (crc32(inner.data(), inner.size()) != crc) { err = "the file is damaged (check value does not match)"; return false; }
    size_t off = 0; header.clear(); if (b[6] & 1) { if (inner.size() < 80) { err = "the file is damaged"; return false; } header.assign(inner.begin(), inner.begin() + 80); off = 80; }
    if (!stlx3::decode(inner.data() + off, inner.size() - off, m.P, m.F)) { err = "the file is damaged (mesh block)"; return false; }
    return true;
}
static Result restore(const Bytes& in, Bytes& out) {
    Result r; r.inBytes = in.size(); Mesh m; Bytes h;
    if (!unpack_mesh(in, m, h, r.error)) return r;
    out = write_stl(m.P, m.F, h.empty() ? nullptr : h.data()); r.outBytes = out.size(); r.triangles = m.nt(); r.vertices = m.nv(); r.ok = true; return r;
}

// ---- compress ------------------------------------------------------------------------------------------------------
static Result compress(const Bytes& in, bool isObj, int packer, Bytes& out, const std::function<void(const char*, double)>& progress) {
    Result r; r.inBytes = in.size(); Mesh m;
    progress("Reading", 0.02);
    if (!(isObj ? load_obj(in, m, r.error) : load_stl(in, m, r.error))) return r;
    r.triangles = m.nt(); r.vertices = m.nv();
    bool binHeader = !isObj && is_binary_stl(in);
    progress("Trying triangle orders", 0.10);
    vector<vector<uint32_t>> orders; orders.push_back(m.F);
    { vector<uint32_t> o(m.F.size()); meshopt_optimizeVertexCache(o.data(), m.F.data(), m.F.size(), m.nv()); orders.push_back(std::move(o)); }
    orders.push_back(walk_order(m));
    Bytes best; size_t bs = SIZE_MAX;
    for (int i = 0; i < 3; i++) {   // pick the order that a quick zstd -3 packs smallest
        Bytes x = stlx3::encode(m.P.data(), m.nv(), orders[i].data(), m.nt()); size_t s = zstd_pack(x, 3).size();
        if (s < bs) { bs = s; best.swap(x); r.order = i; }
        progress("Trying triangle orders", 0.10 + 0.08 * (i + 1));
    }
    Bytes inner; if (binHeader) inner.insert(inner.end(), in.begin(), in.begin() + 80); inner.insert(inner.end(), best.begin(), best.end());
    progress(packer == ZPAQ ? "Packing (smallest, takes longest)" : "Packing", 0.35);
    Bytes packed; if (!pack(packer, inner, packed)) { r.error = "packing failed"; return r; }
    out.assign({'S', 'T', 'L', 'H', 1, (uint8_t)packer, (uint8_t)(binHeader ? 1 : 0), 0});
    uint32_t n = (uint32_t)inner.size(), c = crc32(inner.data(), inner.size());
    for (int i = 0; i < 4; i++) out.push_back((n >> (8 * i)) & 255); for (int i = 0; i < 4; i++) out.push_back((c >> (8 * i)) & 255);
    out.insert(out.end(), packed.begin(), packed.end());
    progress("Checking", 0.75);
    Mesh d; Bytes h; string e;
    if (!unpack_mesh(out, d, h, e) || d.nt() != m.nt() || canon(d.P, d.F) != canon(m.P, m.F)) { r.error = "self-check failed, nothing was saved"; out.clear(); return r; }
    r.verified = true; r.outBytes = out.size(); r.ok = true; progress("Done", 1.0); return r;
}
}  // namespace stlh
