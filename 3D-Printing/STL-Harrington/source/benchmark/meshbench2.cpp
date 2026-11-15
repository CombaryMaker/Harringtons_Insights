// meshbench2 - fair in-memory benchmark of lossless (exact-geometry) mesh coders on binary STL files.
// Every coder starts from the same indexed mesh in memory (float32 positions + triangle list) and ends with it decoded;
// file reading and writing are not timed. One thread. Encode and decode are timed separately (median of N runs).
// Every decoded result is checked: same triangles, bit-exact coordinates, winding kept (triangle order/rotation free).
// Usage: meshbench2 <coder> <file.stl> <runs>     prints one JSON line.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <sys/resource.h>
#include "stlx3.h"
#include "meshoptimizer.h"
#include "openctm.h"
#include "zstd.h"
#include "lzma.h"
#include "libzpaq.h"
#include "draco/compression/encode.h"
#include "draco/compression/decode.h"
#include "draco/mesh/mesh.h"
using namespace std;
typedef vector<uint8_t> Bytes;

void libzpaq::error(const char* msg) { fprintf(stderr, "zpaq error: %s\n", msg); exit(3); }
struct ZR : libzpaq::Reader { const Bytes& b; size_t p = 0; ZR(const Bytes& x) : b(x) {} int get() { return p < b.size() ? b[p++] : -1; } };
struct ZW : libzpaq::Writer { Bytes b; void put(int c) { b.push_back((uint8_t)c); } };

// ---------------- back ends ----------------
static Bytes be_comp(const string& be, const Bytes& in) {
    if (be == "zstd19") { Bytes o(ZSTD_compressBound(in.size())); o.resize(ZSTD_compress(o.data(), o.size(), in.data(), in.size(), 19)); return o; }
    if (be == "zstd3") { Bytes o(ZSTD_compressBound(in.size())); o.resize(ZSTD_compress(o.data(), o.size(), in.data(), in.size(), 3)); return o; }
    if (be == "xz") { Bytes o(lzma_stream_buffer_bound(in.size())); size_t op = 0;
        lzma_easy_buffer_encode(9 | LZMA_PRESET_EXTREME, LZMA_CHECK_NONE, nullptr, in.data(), in.size(), o.data(), &op, o.size()); o.resize(op); return o; }
    if (be == "zpaq5") { ZR r(in); ZW w; libzpaq::compress(&r, &w, "5"); return w.b; }
    fprintf(stderr, "bad back end\n"); exit(2);
}
static Bytes be_decomp(const string& be, const Bytes& in, size_t n) {
    if (be == "zstd19" || be == "zstd3") { Bytes o(n); ZSTD_decompress(o.data(), n, in.data(), in.size()); return o; }
    if (be == "xz") { Bytes o(n); size_t ip = 0, op = 0; uint64_t ml = UINT64_MAX;
        lzma_stream_buffer_decode(&ml, 0, nullptr, in.data(), &ip, in.size(), o.data(), &op, n); return o; }
    if (be == "zpaq5") { ZR r(in); ZW w; libzpaq::decompress(&r, &w); return w.b; }
    exit(2);
}

// ---------------- mesh ----------------
struct Mesh { vector<float> P; vector<uint32_t> F; size_t nv() const { return P.size() / 3; } size_t nt() const { return F.size() / 3; } };
struct K3 { uint32_t a, b, c; bool operator==(const K3& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KH { size_t operator()(const K3& k) const { uint64_t h = k.a * 0x9E3779B97F4A7C15ull ^ (k.b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^ k.c * 0x94D049BB133111EBull; return h ^ (h >> 31); } };
static Mesh load(const char* fn, Bytes& raw) {
    FILE* f = fopen(fn, "rb"); fseek(f, 0, 2); raw.resize(ftell(f)); fseek(f, 0, 0); if (fread(raw.data(), 1, raw.size(), f) != raw.size()) exit(4); fclose(f);
    uint32_t n; memcpy(&n, raw.data() + 80, 4); Mesh m; m.F.resize((size_t)n * 3);
    unordered_map<K3, uint32_t, KH> id; id.reserve((size_t)n * 2);
    for (size_t t = 0; t < n; t++) for (int k = 0; k < 3; k++) {
        K3 key; memcpy(&key, raw.data() + 84 + 50 * t + 12 + 12 * k, 12);
        auto it = id.find(key); uint32_t v;
        if (it == id.end()) { v = (uint32_t)(m.P.size() / 3); id.emplace(key, v); float xyz[3]; memcpy(xyz, &key, 12); m.P.insert(m.P.end(), xyz, xyz + 3); }
        else v = it->second;
        m.F[3 * t + k] = v;
    }
    return m;
}
typedef array<uint32_t, 9> Tri;
static vector<Tri> canon(const vector<float>& P, const vector<uint32_t>& F) {
    vector<Tri> out(F.size() / 3);
    for (size_t t = 0; t < out.size(); t++) {
        Tri best{}; for (int r = 0; r < 3; r++) {
            Tri c; for (int j = 0; j < 3; j++) memcpy(&c[3 * j], &P[3 * (size_t)F[3 * t + (r + j) % 3]], 12);
            if (r == 0 || c < best) best = c;
        }
        out[t] = best;
    }
    sort(out.begin(), out.end()); return out;
}

// ---------------- coders: encode returns bytes, decode returns mesh ----------------
struct Coder { virtual Bytes enc(const Mesh& m) = 0; virtual Mesh dec(const Bytes& b) = 0; virtual ~Coder() {} };

static void wrap(Bytes& o, const Bytes& body, size_t rawlen, uint8_t tag) { o.clear(); o.push_back(tag); for (int i = 0; i < 4; i++) o.push_back((rawlen >> (8 * i)) & 255); o.insert(o.end(), body.begin(), body.end()); }

// stlx3: tries the file's own triangle order, a pre-sorted order (neighbours together, meshoptimizer's vertex-cache sort)
// and a depth-first walk across shared sides; picks the order with the smallest zstd -3 size, then the final back end.
static vector<uint32_t> walk_order(const Mesh& m) {
    size_t nt = m.nt(); unordered_map<uint64_t, vector<uint32_t>> edge; edge.reserve(nt * 2);
    for (size_t t = 0; t < nt; t++) for (int k = 0; k < 3; k++) { uint32_t u = m.F[3 * t + k], v = m.F[3 * t + (k + 1) % 3]; edge[stlx3::ek(min(u, v), max(u, v))].push_back((uint32_t)t); }
    vector<char> seen(nt, 0); vector<uint32_t> out; out.reserve(nt * 3); vector<uint32_t> st;
    for (size_t t0 = 0; t0 < nt; t0++) {
        if (seen[t0]) continue; seen[t0] = 1; st.push_back((uint32_t)t0);
        while (!st.empty()) {
            uint32_t t = st.back(); st.pop_back(); out.insert(out.end(), &m.F[3 * t], &m.F[3 * t] + 3);
            uint32_t a = m.F[3 * t], b = m.F[3 * t + 1], c = m.F[3 * t + 2]; uint32_t sides[3][2] = {{c, a}, {b, c}, {a, b}};
            for (auto& s : sides) for (uint32_t n : edge[stlx3::ek(min(s[0], s[1]), max(s[0], s[1]))]) if (!seen[n]) { seen[n] = 1; st.push_back(n); }
        }
    }
    return out;
}
struct STLX3 : Coder {
    string be; int fixed; STLX3(string b, int f) : be(b), fixed(f) {}
    Bytes enc(const Mesh& m) override {
        vector<vector<uint32_t>> orders;
        if (fixed < 0 || fixed == 0) orders.push_back(m.F);
        if (fixed < 0 || fixed == 1) { vector<uint32_t> o(m.F.size()); meshopt_optimizeVertexCache(o.data(), m.F.data(), m.F.size(), m.nv()); orders.push_back(o); }
        if (fixed < 0 || fixed == 2) orders.push_back(walk_order(m));
        Bytes best; size_t bs = SIZE_MAX;
        for (auto& o : orders) {
            Bytes x = stlx3::encode(m.P.data(), m.nv(), o.data(), m.nt());
            size_t s = orders.size() > 1 ? be_comp("zstd3", x).size() : 0;
            if (s < bs) { bs = s; best.swap(x); }
        }
        Bytes o; wrap(o, be_comp(be, best), best.size(), 1); return o;
    }
    Mesh dec(const Bytes& b) override {
        size_t n = b[1] | (b[2] << 8) | (b[3] << 16) | ((size_t)b[4] << 24); Bytes x = be_decomp(be, Bytes(b.begin() + 5, b.end()), n);
        Mesh m; stlx3::decode(x.data(), x.size(), m.P, m.F); return m;
    }
};
// meshoptimizer: vertex-cache order, vertex-fetch order, its lossless index and vertex codecs, then a back end.
struct MESHOPT : Coder {
    string be; int level; MESHOPT(string b, int l) : be(b), level(l) {}
    Bytes enc(const Mesh& m) override {
        size_t ni = m.F.size(), nv = m.nv(); vector<uint32_t> ix(ni); meshopt_optimizeVertexCache(ix.data(), m.F.data(), ni, nv);
        vector<float> vf(nv * 3); size_t nu = meshopt_optimizeVertexFetch(vf.data(), ix.data(), ni, m.P.data(), nv, 12);
        Bytes eb(meshopt_encodeVertexBufferBound(nu, 12)); eb.resize(meshopt_encodeVertexBufferLevel(eb.data(), eb.size(), vf.data(), nu, 12, level, 1));
        Bytes ib(meshopt_encodeIndexBufferBound(ni, nu)); ib.resize(meshopt_encodeIndexBuffer(ib.data(), ib.size(), ix.data(), ni));
        Bytes body; stlx3::put32(body, (uint32_t)nu); stlx3::put32(body, (uint32_t)ni); stlx3::put32(body, (uint32_t)eb.size());
        body.insert(body.end(), eb.begin(), eb.end()); body.insert(body.end(), ib.begin(), ib.end());
        Bytes o; wrap(o, be_comp(be, body), body.size(), 2); return o;
    }
    Mesh dec(const Bytes& b) override {
        size_t n = b[1] | (b[2] << 8) | (b[3] << 16) | ((size_t)b[4] << 24); Bytes x = be_decomp(be, Bytes(b.begin() + 5, b.end()), n);
        uint32_t nu = stlx3::get32(&x[0]), ni = stlx3::get32(&x[4]), le = stlx3::get32(&x[8]);
        Mesh m; m.P.resize((size_t)nu * 3); m.F.resize(ni);
        meshopt_decodeVertexBuffer(m.P.data(), nu, 12, &x[12], le); meshopt_decodeIndexBuffer(m.F.data(), ni, 4, &x[12 + le], x.size() - 12 - le);
        return m;
    }
};
// OpenCTM MG1 (its lossless method, LZMA inside), in memory.
static CTMuint wfn(const void* buf, CTMuint size, void* ud) { Bytes* o = (Bytes*)ud; o->insert(o->end(), (const uint8_t*)buf, (const uint8_t*)buf + size); return size; }
struct RD { const Bytes* b; size_t p; };
static CTMuint rfn(void* buf, CTMuint size, void* ud) { RD* r = (RD*)ud; size_t n = min((size_t)size, r->b->size() - r->p); memcpy(buf, r->b->data() + r->p, n); r->p += n; return (CTMuint)n; }
struct CTM : Coder {
    Bytes enc(const Mesh& m) override {
        CTMcontext c = ctmNewContext(CTM_EXPORT); ctmDefineMesh(c, m.P.data(), (CTMuint)m.nv(), m.F.data(), (CTMuint)m.nt(), NULL);
        ctmCompressionMethod(c, CTM_METHOD_MG1); ctmCompressionLevel(c, 9); Bytes o; ctmSaveCustom(c, wfn, &o); ctmFreeContext(c); return o;
    }
    Mesh dec(const Bytes& b) override {
        CTMcontext c = ctmNewContext(CTM_IMPORT); RD r{&b, 0}; ctmLoadCustom(c, rfn, &r);
        CTMuint nv = ctmGetInteger(c, CTM_VERTEX_COUNT), nt = ctmGetInteger(c, CTM_TRIANGLE_COUNT); Mesh m;
        const float* p = ctmGetFloatArray(c, CTM_VERTICES); const CTMuint* f = ctmGetIntegerArray(c, CTM_INDICES);
        m.P.assign(p, p + (size_t)nv * 3); m.F.assign(f, f + (size_t)nt * 3); ctmFreeContext(c); return m;
    }
};
// Draco: Edgebreaker, speed 0 (= -cl 10), positions not quantized (lossless floats).
struct DRACO : Coder {
    Bytes enc(const Mesh& m) override {
        draco::Mesh dm; dm.set_num_points((uint32_t)m.nv());
        draco::GeometryAttribute ga; ga.Init(draco::GeometryAttribute::POSITION, nullptr, 3, draco::DT_FLOAT32, false, 12, 0);
        int id = dm.AddAttribute(ga, true, (uint32_t)m.nv()); draco::PointAttribute* pa = dm.attribute(id);
        for (size_t i = 0; i < m.nv(); i++) pa->SetAttributeValue(draco::AttributeValueIndex((uint32_t)i), &m.P[3 * i]);
        for (size_t t = 0; t < m.nt(); t++) { draco::Mesh::Face f; for (int k = 0; k < 3; k++) f[k] = draco::PointIndex(m.F[3 * t + k]); dm.AddFace(f); }
        draco::Encoder e; e.SetSpeedOptions(0, 0); e.SetEncodingMethod(draco::MESH_EDGEBREAKER_ENCODING);
        draco::EncoderBuffer buf; if (!e.EncodeMeshToBuffer(dm, &buf).ok()) { fprintf(stderr, "draco encode failed\n"); exit(5); }
        return Bytes(buf.data(), buf.data() + buf.size());
    }
    Mesh dec(const Bytes& b) override {
        draco::DecoderBuffer db; db.Init((const char*)b.data(), b.size()); draco::Decoder d; auto r = d.DecodeMeshFromBuffer(&db);
        auto dm = std::move(r).value(); const draco::PointAttribute* pa = dm->GetNamedAttribute(draco::GeometryAttribute::POSITION);
        Mesh m; m.P.resize((size_t)dm->num_points() * 3);
        for (uint32_t i = 0; i < dm->num_points(); i++) pa->GetMappedValue(draco::PointIndex(i), &m.P[3 * (size_t)i]);
        m.F.resize((size_t)dm->num_faces() * 3);
        for (uint32_t f = 0; f < dm->num_faces(); f++) for (int k = 0; k < 3; k++) m.F[3 * (size_t)f + k] = dm->face(draco::FaceIndex(f))[k].value();
        return m;
    }
};
// The raw STL file itself through a back end (byte-exact file, for reference).
struct RAW : Coder {
    string be; const Bytes* raw; RAW(string b, const Bytes* r) : be(b), raw(r) {}
    Bytes enc(const Mesh&) override { Bytes o; wrap(o, be_comp(be, *raw), raw->size(), 3); return o; }
    Mesh dec(const Bytes& b) override { size_t n = b[1] | (b[2] << 8) | (b[3] << 16) | ((size_t)b[4] << 24); Bytes x = be_decomp(be, Bytes(b.begin() + 5, b.end()), n);
        Mesh m; m.P.assign((const float*)nullptr, (const float*)nullptr); m.F.clear(); m.P.push_back(x == *raw ? 1.f : 0.f); return m; }
};

static double now() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: meshbench2 <coder> <file.stl> <runs>\n"); return 1; }
    string cn = argv[1]; int runs = atoi(argv[3]); Bytes raw; Mesh m = load(argv[2], raw);
    Coder* c = nullptr;
    auto after = [&](const string& p) { return cn.substr(p.size()); };
    if (cn.rfind("stlx3+", 0) == 0) c = new STLX3(after("stlx3+"), -1);
    else if (cn.rfind("stlx3file+", 0) == 0) c = new STLX3(after("stlx3file+"), 0);
    else if (cn.rfind("meshopt+", 0) == 0) c = new MESHOPT(after("meshopt+"), 2);
    else if (cn == "openctm-mg1") c = new CTM();
    else if (cn == "draco-qp0") c = new DRACO();
    else if (cn.rfind("raw+", 0) == 0) c = new RAW(after("raw+"), &raw);
    else if (cn == "load") { struct rusage ru; getrusage(RUSAGE_SELF, &ru); printf("{\"coder\":\"load\",\"maxrss_kb\":%ld}\n", ru.ru_maxrss); return 0; }
    else { fprintf(stderr, "unknown coder\n"); return 1; }
    vector<double> te, td; Bytes enc; Mesh dm;
    for (int r = 0; r < runs; r++) { double t = now(); enc = c->enc(m); te.push_back(now() - t); }
    for (int r = 0; r < runs; r++) { double t = now(); dm = c->dec(enc); td.push_back(now() - t); }
    sort(te.begin(), te.end()); sort(td.begin(), td.end());
    bool exact; if (cn.rfind("raw+", 0) == 0) exact = dm.P.size() == 1 && dm.P[0] == 1.f;
    else exact = dm.nt() == m.nt() && canon(dm.P, dm.F) == canon(m.P, m.F);
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    printf("{\"coder\":\"%s\",\"file\":\"%s\",\"stl_bytes\":%zu,\"triangles\":%zu,\"vertices\":%zu,\"bytes\":%zu,\"exact\":%s,"
           "\"enc_s\":%.6f,\"dec_s\":%.6f,\"runs\":%d,\"maxrss_kb\":%ld}\n", cn.c_str(), argv[2], raw.size(), m.nt(), m.nv(), enc.size(),
           exact ? "true" : "false", te[te.size() / 2], td[td.size() / 2], runs, ru.ru_maxrss);
    return 0;
}
