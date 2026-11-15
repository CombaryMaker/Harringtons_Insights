// Harrington-3MF Research Encoder - core (first prototype, 15 Nov 2026). Michael Hugh Harrington, MIT License.
// Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.
// Turns an STL (binary or ASCII) or OBJ mesh into a standard 3MF file that ordinary slicers open.
// Smaller than ordinary 3MF because of: the shortest number text that reads back as exactly the same value, minimal XML,
// a neighbour-sorted triangle order when it helps, and the strongest standard deflate (zopfli). Every coordinate is kept
// exactly; the finished file is unpacked and read back before it is reported as verified.
// Uses: zopfli (Google, Apache 2.0), miniz (MIT), meshoptimizer vertex-cache / vertex-fetch order (Arseny Kapoulkine, MIT).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <charconv>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <cstdlib>
extern "C" {
#include "zopfli/zopfli.h"
#include "zopfli/deflate.h"
#include "miniz.h"
#include "miniz_tdef.h"
#include "miniz_tinfl.h"
}
#include "meshoptimizer.h"

namespace s3mf {
using std::string; using std::vector;
struct Mesh { vector<float> P; vector<uint32_t> F; size_t dropped = 0; size_t nv() const { return P.size() / 3; } size_t nt() const { return F.size() / 3; } };
struct Result { bool ok = false; string error; size_t inBytes = 0, outBytes = 0, triangles = 0, vertices = 0, dropped = 0; bool verified = false; bool sorted = false; };

struct K3 { uint32_t a, b, c; bool operator==(const K3& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KH { size_t operator()(const K3& k) const { uint64_t h = k.a * 0x9E3779B97F4A7C15ull ^ (k.b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^ k.c * 0x94D049BB133111EBull; return (size_t)(h ^ (h >> 31)); } };

// ---- reading -------------------------------------------------------------------------------------------------------
static void add_tri(Mesh& m, std::unordered_map<K3, uint32_t, KH>& id, const float* v9) {
    uint32_t ix[3];
    for (int k = 0; k < 3; k++) {
        K3 key; memcpy(&key, v9 + 3 * k, 12); auto it = id.find(key);
        if (it == id.end()) { ix[k] = (uint32_t)m.nv(); id.emplace(key, ix[k]); m.P.insert(m.P.end(), v9 + 3 * k, v9 + 3 * k + 3); }
        else ix[k] = it->second;
    }
    if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) { m.dropped++; return; }   // 3MF forbids triangles that repeat a vertex
    m.F.insert(m.F.end(), ix, ix + 3);
}
static bool read_float(const char*& p, const char* e, float& out) {   // one number token -> float (correctly rounded, C locale)
    char b[72]; int n = 0;
    while (p < e && n < 70 && ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) b[n++] = *p++;
    if (n == 0 || n >= 70) return false; b[n] = 0; char* end = nullptr; out = strtof(b, &end); return end == b + n;
}
static bool parse_float(const char*& p, const char* e, float& out) {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return read_float(p, e, out);
}
static bool load_stl(const vector<uint8_t>& b, Mesh& m, string& err) {
    std::unordered_map<K3, uint32_t, KH> id;
    bool binary = false;
    if (b.size() >= 84) { uint32_t n; memcpy(&n, &b[80], 4); if ((uint64_t)84 + 50ull * n == b.size()) binary = true; }
    if (binary) {
        uint32_t n; memcpy(&n, &b[80], 4); id.reserve((size_t)n * 2); m.F.reserve((size_t)n * 3);
        for (size_t t = 0; t < n; t++) { float v[9]; memcpy(v, &b[84 + 50 * t + 12], 36); add_tri(m, id, v); }
        return true;
    }
    const char* p = (const char*)b.data(); const char* e = p + b.size(); float v[9]; int k = 0;
    while (p < e) {   // ASCII STL: collect every "vertex x y z"
        const char* q = (const char*)memchr(p, 'v', e - p); if (!q) break;
        if (e - q >= 6 && memcmp(q, "vertex", 6) == 0 && (q == (const char*)b.data() || q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\n' || q[-1] == '\r')) {
            p = q + 6; float x, y, z;
            if (!parse_float(p, e, x) || !parse_float(p, e, y) || !parse_float(p, e, z)) { err = "unreadable number in ASCII STL"; return false; }
            v[3 * k] = x; v[3 * k + 1] = y; v[3 * k + 2] = z; if (++k == 3) { add_tri(m, id, v); k = 0; }
        } else p = q + 1;
    }
    if (m.nt() == 0 && m.dropped == 0) { err = "no triangles found (not an STL file?)"; return false; }
    return true;
}
static bool load_obj(const vector<uint8_t>& b, Mesh& m, string& err) {
    std::unordered_map<K3, uint32_t, KH> id; vector<float> V; const char* p = (const char*)b.data(); const char* e = p + b.size();
    while (p < e) {
        const char* nl = (const char*)memchr(p, '\n', e - p); const char* le = nl ? nl : e;
        while (p < le && (*p == ' ' || *p == '\t')) p++;
        if (le - p > 2 && p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2; float x, y, z; if (!parse_float(p, le, x) || !parse_float(p, le, y) || !parse_float(p, le, z)) { err = "unreadable vertex in OBJ"; return false; }
            V.push_back(x); V.push_back(y); V.push_back(z);
        } else if (le - p > 2 && p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            p += 2; vector<long> idx;
            while (p < le) {
                while (p < le && (*p == ' ' || *p == '\t' || *p == '\r')) p++; if (p >= le) break;
                long i = 0; auto r = std::from_chars(p, le, i); if (r.ec != std::errc()) { err = "unreadable face in OBJ"; return false; }
                p = r.ptr; while (p < le && *p != ' ' && *p != '\t' && *p != '\r') p++;   // skip /vt/vn
                long n = (long)(V.size() / 3); long j = i < 0 ? n + i : i - 1; if (j < 0 || j >= n) { err = "face points to a missing vertex"; return false; }
                idx.push_back(j);
            }
            for (size_t t = 1; t + 1 < idx.size(); t++) {   // fan for polygons
                float v9[9]; long c[3] = {idx[0], idx[t], idx[t + 1]};
                for (int k = 0; k < 3; k++) memcpy(v9 + 3 * k, &V[3 * c[k]], 12);
                add_tri(m, id, v9);
            }
        }
        p = le + 1;
    }
    if (m.nt() == 0) { err = "no faces found in OBJ"; return false; }
    return true;
}

// ---- writing --------------------------------------------------------------------------------------------------------
static void put_num(string& s, float f) {
    char b[48]; auto r = std::to_chars(b, b + sizeof b, f); s.append(b, r.ptr);
}
static string model_xml(const vector<float>& P, const vector<uint32_t>& F) {
    string s; s.reserve(P.size() * 12 + F.size() * 10 + 512);
    s += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">"
         "<metadata name=\"Application\">Harrington-3MF Research Encoder (first prototype)</metadata><resources><object id=\"1\" type=\"model\"><mesh><vertices>";
    for (size_t i = 0; i < P.size(); i += 3) { s += "<vertex x=\""; put_num(s, P[i]); s += "\" y=\""; put_num(s, P[i + 1]); s += "\" z=\""; put_num(s, P[i + 2]); s += "\"/>"; }
    s += "</vertices><triangles>";
    char b[16];
    for (size_t i = 0; i < F.size(); i += 3) {
        s += "<triangle v1=\""; s.append(b, std::to_chars(b, b + 16, F[i]).ptr); s += "\" v2=\""; s.append(b, std::to_chars(b, b + 16, F[i + 1]).ptr);
        s += "\" v3=\""; s.append(b, std::to_chars(b, b + 16, F[i + 2]).ptr); s += "\"/>";
    }
    s += "</triangles></mesh></object></resources><build><item objectid=\"1\"/></build></model>";
    return s;
}
static const char* CT = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>";
static const char* RELS = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>";

static vector<uint8_t> deflate_fast(const string& s) {   // quick deflate, only used to choose the better triangle order
    size_t outLen = 0; void* p = tdefl_compress_mem_to_heap(s.data(), s.size(), &outLen, TDEFL_DEFAULT_MAX_PROBES);
    vector<uint8_t> v((uint8_t*)p, (uint8_t*)p + outLen); free(p); return v;
}
static vector<uint8_t> deflate_best(const string& s, int iterations) {
    ZopfliOptions o; ZopfliInitOptions(&o); o.numiterations = iterations;
    unsigned char* out = nullptr; size_t outsize = 0; unsigned char bp = 0;
    ZopfliDeflate(&o, 2, 1, (const unsigned char*)s.data(), s.size(), &bp, &out, &outsize);
    vector<uint8_t> v(out, out + outsize); free(out); return v;
}
static uint32_t crc32(const string& s) { return (uint32_t)mz_crc32(MZ_CRC32_INIT, (const unsigned char*)s.data(), s.size()); }
static void p16(vector<uint8_t>& o, uint32_t v) { o.push_back(v & 255); o.push_back((v >> 8) & 255); }
static void p32(vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((v >> (8 * i)) & 255); }
struct Entry { string name, data; vector<uint8_t> comp; };
static vector<uint8_t> zip(vector<Entry>& es) {
    vector<uint8_t> out, cd;
    for (auto& e : es) {
        uint32_t crc = crc32(e.data), off = (uint32_t)out.size();
        p32(out, 0x04034b50); p16(out, 20); p16(out, 0); p16(out, 8); p16(out, 0); p16(out, 0x21); p32(out, crc);
        p32(out, (uint32_t)e.comp.size()); p32(out, (uint32_t)e.data.size()); p16(out, (uint32_t)e.name.size()); p16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end()); out.insert(out.end(), e.comp.begin(), e.comp.end());
        p32(cd, 0x02014b50); p16(cd, 20); p16(cd, 20); p16(cd, 0); p16(cd, 8); p16(cd, 0); p16(cd, 0x21); p32(cd, crc);
        p32(cd, (uint32_t)e.comp.size()); p32(cd, (uint32_t)e.data.size()); p16(cd, (uint32_t)e.name.size()); p16(cd, 0); p16(cd, 0); p16(cd, 0); p16(cd, 0); p32(cd, 0); p32(cd, off);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    uint32_t cdoff = (uint32_t)out.size(); out.insert(out.end(), cd.begin(), cd.end());
    p32(out, 0x06054b50); p16(out, 0); p16(out, 0); p16(out, (uint32_t)es.size()); p16(out, (uint32_t)es.size()); p32(out, (uint32_t)cd.size()); p32(out, cdoff); p16(out, 0);
    return out;
}

// ---- checking: unpack the finished file and read every number back -------------------------------------------------
static bool verify(const vector<uint8_t>& z, const vector<float>& P, const vector<uint32_t>& F) {
    // find the model entry
    size_t p = 0; while (p + 30 <= z.size() && z[p] == 0x50 && z[p + 1] == 0x4b && z[p + 2] == 3 && z[p + 3] == 4) {
        uint32_t csz, usz; uint16_t nl, xl; memcpy(&csz, &z[p + 18], 4); memcpy(&usz, &z[p + 22], 4); memcpy(&nl, &z[p + 26], 2); memcpy(&xl, &z[p + 28], 2);
        string name((const char*)&z[p + 30], nl); size_t d = p + 30 + nl + xl;
        if (name == "3D/3dmodel.model") {
            string s(usz, '\0'); size_t got = tinfl_decompress_mem_to_mem(&s[0], usz, &z[d], csz, 0); if (got != usz) return false;
            uint32_t crc; memcpy(&crc, &z[p + 14], 4); if (crc32(s) != crc) return false;
            const char* q = s.data(); const char* e = q + s.size(); size_t vi = 0, ti = 0;
            while ((q = (const char*)memchr(q, '<', e - q)) != nullptr) {
                if (e - q > 8 && memcmp(q, "<vertex ", 8) == 0) {
                    for (const char* a : {"x=\"", "y=\"", "z=\""}) {
                        const char* f = strstr(q, a); if (!f) return false; f += 3; float v;
                        if (!read_float(f, e, v) || vi >= P.size() || memcmp(&v, &P[vi], 4) != 0) return false; vi++;   // bit-exact, -0 included
                    }
                } else if (e - q > 10 && memcmp(q, "<triangle ", 10) == 0) {
                    for (const char* a : {"v1=\"", "v2=\"", "v3=\""}) {
                        const char* f = strstr(q, a); if (!f) return false; f += 4; uint32_t v; auto r = std::from_chars(f, e, v);
                        if (r.ec != std::errc() || ti >= F.size() || v != F[ti]) return false; ti++;
                    }
                }
                q++;
            }
            return vi == P.size() && ti == F.size();
        }
        p = d + csz;
    }
    return false;
}

// ---- the whole job ----------------------------------------------------------------------------------------------------
// progress(stage text, 0..1). Returns the 3MF bytes in out.
static Result encode(const vector<uint8_t>& in, bool isObj, vector<uint8_t>& out, const std::function<void(const char*, double)>& progress) {
    Result R; R.inBytes = in.size(); Mesh m; string err;
    progress("Reading the mesh", 0.02);
    if (!(isObj ? load_obj(in, m, err) : load_stl(in, m, err))) { R.error = err; return R; }
    R.triangles = m.nt(); R.vertices = m.nv(); R.dropped = m.dropped;
    if (m.nt() == 0) { R.error = "the mesh has no usable triangles"; return R; }
    progress("Choosing the best triangle order", 0.10);
    // order A: as in the file. Order B: neighbours together (vertex-cache order, vertices renumbered by first use).
    vector<uint32_t> FB(m.F.size()); meshopt_optimizeVertexCache(FB.data(), m.F.data(), m.F.size(), m.nv());
    vector<float> PB(m.P.size()); meshopt_optimizeVertexFetch(PB.data(), FB.data(), FB.size(), m.P.data(), m.nv(), 12);
    string xa = model_xml(m.P, m.F), xb = model_xml(PB, FB);
    bool useB = deflate_fast(xb).size() < deflate_fast(xa).size(); R.sorted = useB;
    const string& x = useB ? xb : xa; const vector<float>& P = useB ? PB : m.P; const vector<uint32_t>& F = useB ? FB : m.F;
    int it = x.size() < 4000000 ? 15 : x.size() < 20000000 ? 5 : x.size() < 60000000 ? 2 : 1;
    progress("Packing with the strongest deflate (the slow step)", 0.25);
    vector<Entry> es(3); es[0].name = "[Content_Types].xml"; es[0].data = CT; es[1].name = "_rels/.rels"; es[1].data = RELS; es[2].name = "3D/3dmodel.model"; es[2].data = x;
    for (auto& e : es) e.comp = deflate_best(e.data, it);
    out = zip(es);
    progress("Checking the finished file", 0.95);
    R.verified = verify(out, P, F);
    if (!R.verified) { R.error = "self-check failed - file not written"; out.clear(); return R; }
    R.outBytes = out.size(); R.ok = true; return R;
}
}  // namespace s3mf
