// hdeflate.cpp - H-Deflate: find deflate streams in a file (PNG, ZIP, gzip, zlib/PDF), replace each with its raw
// bytes plus a reconstruction record; unpack rebuilds the file byte for byte.
// usage: hdeflate pack IN OUT [threads]     hdeflate unpack IN OUT [threads]     hdeflate test IN [threads] [stats.csv]
//        hdeflate zcheck FILE               (development only; needs -DHAVE_ZLIB -lz: compares with the system zlib)
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See H-Deflate/LICENSE.txt.
#include "hd_search.h"
#include "hd_inflate_fast.h"
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif
using namespace hd;

static std::vector<uint8_t> read_file(const char* fn) {
    FILE* f = fopen(fn, "rb"); if (!f) { perror(fn); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> v((size_t)n); if (n && fread(v.data(), 1, (size_t)n, f) != (size_t)n) { perror("read"); exit(1); }
    fclose(f); return v;
}
static void write_file(const char* fn, const std::vector<uint8_t>& v) {
    FILE* f = fopen(fn, "wb"); if (!f) { perror(fn); exit(1); }
    if (v.size() && fwrite(v.data(), 1, v.size(), f) != v.size()) { perror("write"); exit(1); }
    fclose(f);
}

static uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n) { size_t k = n < 5552 ? n : 5552; n -= k; while (k--) { a += *p++; b += a; } a %= 65521; b %= 65521; }
    return (b << 16) | a;
}
static uint32_t crc_table[256];
static void crc_init() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_table[i] = c; } }
static uint32_t crc32buf(uint32_t c, const uint8_t* p, size_t n) { c = ~c; while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8); return ~c; }
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t* p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(((uint32_t)p[1] << 8) | p[0]); }

// ---------------------------------------------------------------- stream regions
enum Kind : uint8_t { K_ZLIB = 1, K_RAW = 2, K_GZIP = 3, K_PNG = 4 };
static const char* kind_name(int k) { return k == K_ZLIB ? "zlib" : k == K_RAW ? "zip" : k == K_GZIP ? "gzip" : k == K_PNG ? "png" : "?"; }

struct Region {
    Kind kind; size_t start = 0, end = 0;     // byte span in the original file replaced by this stream
    std::vector<uint8_t> comp;                // the deflate bytes (joined for PNG)
    std::vector<uint32_t> png_chunks;         // IDAT chunk data lengths
    uint32_t png_w = 0, png_h = 0; int png_bd = 0, png_ct = 0, png_il = 0;   // IHDR
    std::vector<uint8_t> filters; size_t pnm_hdr_len = 0; bool pdf_pred = false; int img_mode = 0;   // 1 = unfiltered pixels + header, 2 = header only
    uint8_t zhdr[2] = {0, 0};                 // zlib header (zlib/png)
    // results
    Inflated inf; SearchResult sr; bool ok = false; std::string note;
};

static const uint16_t common_zhdr[] = {0x789C, 0x78DA, 0x7801, 0x785E, 0x6881, 0x58C3, 0x48C9, 0x38AD, 0x28B1, 0x1885, 0x0899, 0x789E, 0x78BB, 0x7899};

// try to inflate a raw deflate stream at p; min_comp bytes required
static bool try_stream(const uint8_t* p, size_t avail, Inflated& inf, size_t min_comp) {
    if (avail < min_comp) return false;
    if (!inflate_record(p, avail, inf, (size_t)1 << 30)) return false;
    return inf.comp_len >= min_comp;
}

// PDF image XObject: find the dictionary before a FlateDecode stream and read Width/Height/BitsPerComponent/ColorSpace/Predictor
static long pdf_num(const std::string& dict, const char* key) {
    size_t p = dict.find(key); if (p == std::string::npos) return -1; p += strlen(key);
    while (p < dict.size() && (dict[p] == ' ' || dict[p] == '\r' || dict[p] == '\n')) p++;
    if (p >= dict.size() || !isdigit((unsigned char)dict[p])) return -1;
    return atol(dict.c_str() + p);
}
static void pdf_image_dict(const uint8_t* d, size_t n, size_t pos, Region& r) {
    (void)n; size_t back = pos > 1200 ? pos - 1200 : 0;
    std::string s((const char*)d + back, pos - back);
    size_t st = s.rfind("stream"); if (st == std::string::npos) return;
    size_t ob = s.rfind("obj", st); size_t dict0 = s.rfind("<<", st); if (dict0 == std::string::npos) return;
    if (ob != std::string::npos && ob > dict0) dict0 = s.rfind("<<", ob); if (dict0 == std::string::npos) return;
    std::string dict = s.substr(dict0, st - dict0);
    if (dict.find("/Image") == std::string::npos || dict.find("/FlateDecode") == std::string::npos) return;
    long w = pdf_num(dict, "/Width"), h = pdf_num(dict, "/Height"), bpc = pdf_num(dict, "/BitsPerComponent");
    if (w <= 0 || h <= 0 || !(bpc == 8 || bpc == 16 || bpc == 1 || bpc == 2 || bpc == 4)) return;
    int ct;
    if (dict.find("/DeviceRGB") != std::string::npos) ct = 2; else if (dict.find("/DeviceGray") != std::string::npos) ct = 0; else return;
    long colors = pdf_num(dict, "/Colors"); if (colors > 0 && colors != (ct == 2 ? 3 : 1)) return;
    long pred = pdf_num(dict, "/Predictor"); if (pred > 1 && pred < 10) return;   // TIFF predictor: not handled
    if (dict.find("/SMask") != std::string::npos && false) return;
    if (dict.find("/ImageMask") != std::string::npos) return;
    r.png_w = (uint32_t)w; r.png_h = (uint32_t)h; r.png_bd = (int)bpc; r.png_ct = ct; r.png_il = 0; r.pdf_pred = pred >= 10;
}

static std::vector<Region> scan(const uint8_t* d, size_t n, size_t min_comp, bool verbose) {
    std::vector<Region> regs;
    std::vector<uint8_t> covered(n + 1, 0);
    auto take = [&](Region& r) { for (size_t i = r.start; i < r.end; i++) covered[i] = 1; regs.push_back(std::move(r)); };
    // PNG files and PNGs embedded anywhere (OLE pictures, ZIP entries once unpacked)
    for (size_t sig = 0; sig + 8 < n; sig++) {
        if (d[sig] != 0x89 || memcmp(d + sig, "\x89PNG\r\n\x1a\n", 8) || covered[sig]) continue;
        size_t i = sig + 8; Region r; r.kind = K_PNG; bool first = true; size_t lastend = 0;
        std::vector<uint8_t> joined;
        if (i + 25 <= n && be32(d + i) == 13 && !memcmp(d + i + 4, "IHDR", 4)) { r.png_w = be32(d + i + 8); r.png_h = be32(d + i + 12); r.png_bd = d[i + 16]; r.png_ct = d[i + 17]; r.png_il = d[i + 20]; }
        while (i + 12 <= n) {
            uint32_t len = be32(d + i); if ((size_t)len > n - i - 12) break;
            if (!memcmp(d + i + 4, "IDAT", 4)) {
                if (first) { r.start = i; first = false; }
                if (crc32buf(0, d + i + 4, 4 + len) != be32(d + i + 8 + len)) { joined.clear(); break; }
                joined.insert(joined.end(), d + i + 8, d + i + 8 + len); r.png_chunks.push_back(len); lastend = i + 12 + len;
            } else if (!first) break;   // IDATs must be contiguous
            else if (memcmp(d + i + 4, "IHDR", 4) && memcmp(d + i + 4, "PLTE", 4) && memcmp(d + i + 4, "tRNS", 4) && memcmp(d + i + 4, "gAMA", 4) && memcmp(d + i + 4, "cHRM", 4) && memcmp(d + i + 4, "sRGB", 4) && memcmp(d + i + 4, "iCCP", 4) && memcmp(d + i + 4, "bKGD", 4) && memcmp(d + i + 4, "pHYs", 4) && memcmp(d + i + 4, "tEXt", 4) && memcmp(d + i + 4, "zTXt", 4) && memcmp(d + i + 4, "iTXt", 4) && memcmp(d + i + 4, "tIME", 4) && memcmp(d + i + 4, "sBIT", 4) && memcmp(d + i + 4, "hIST", 4) && memcmp(d + i + 4, "sPLT", 4) && memcmp(d + i + 4, "oFFs", 4) && memcmp(d + i + 4, "acTL", 4) && memcmp(d + i + 4, "fcTL", 4) && memcmp(d + i + 4, "eXIf", 4)) break;
            i += 12 + len;
        }
        if (joined.size() > 6 && (joined[0] & 15) == 8 && (joined[0] >> 4) <= 7 && ((joined[0] << 8) | joined[1]) % 31 == 0 && !(joined[1] & 0x20)) {
            Inflated inf;
            if (try_stream(joined.data() + 2, joined.size() - 2, inf, min_comp) && inf.comp_len + 6 == joined.size() && adler32(inf.raw.data(), inf.raw.size()) == be32(joined.data() + 2 + inf.comp_len)) {
                bool ov = false; for (size_t k = r.start; k < lastend; k++) if (covered[k]) { ov = true; break; }
                if (!ov) { r.end = lastend; r.zhdr[0] = joined[0]; r.zhdr[1] = joined[1];
                    r.comp.assign(joined.begin() + 2, joined.begin() + 2 + inf.comp_len); r.inf = std::move(inf); take(r); sig = lastend - 1; }
            }
        }
    }
    // ZIP local entries
    for (size_t i = 0; i + 30 <= n; i++) {
        if (d[i] != 'P' || memcmp(d + i, "PK\x03\x04", 4)) continue;
        uint16_t flg = le16(d + i + 6), meth = le16(d + i + 8), nl = le16(d + i + 26), el = le16(d + i + 28); uint32_t csz = le32(d + i + 18);
        size_t data = i + 30 + nl + el; if (meth != 8 || data >= n || covered[data]) continue;
        Inflated inf;
        if (!try_stream(d + data, n - data, inf, min_comp)) continue;
        if (!(flg & 8) && inf.comp_len != csz) continue;
        Region r; r.kind = K_RAW; r.start = data; r.end = data + inf.comp_len; r.comp.assign(d + data, d + data + inf.comp_len); r.inf = std::move(inf); take(r);
    }
    // gzip members
    for (size_t i = 0; i + 18 <= n; i++) {
        if (d[i] != 0x1f || d[i + 1] != 0x8b || d[i + 2] != 8 || covered[i]) continue;
        uint8_t flg = d[i + 3]; size_t p = i + 10; bool bad = false;
        if (flg & 4) { if (p + 2 > n) continue; p += 2 + le16(d + p); }
        if (flg & 8) { while (p < n && d[p]) p++; p++; }
        if (flg & 16) { while (p < n && d[p]) p++; p++; }
        if (flg & 2) p += 2;
        if (bad || p >= n || covered[p]) continue;
        Inflated inf;
        if (!try_stream(d + p, n - p, inf, min_comp)) continue;
        if (p + inf.comp_len + 8 > n || crc32buf(0, inf.raw.data(), inf.raw.size()) != le32(d + p + inf.comp_len)) continue;
        Region r; r.kind = K_GZIP; r.start = p; r.end = p + inf.comp_len; r.comp.assign(d + p, d + p + inf.comp_len); r.inf = std::move(inf); take(r);
    }
    // zlib streams anywhere (PDF FlateDecode, embedded pictures, ...)
    for (size_t i = 0; i + 8 <= n; i++) {
        if (covered[i]) continue;
        uint8_t c = d[i], g = d[i + 1];
        if ((c & 15) != 8 || (c >> 4) > 7 || ((c << 8) | g) % 31 != 0 || (g & 0x20)) continue;
        // cheap pre-check: first deflate block must be plausible (type != 3)
        if (((d[i + 2] >> 1) & 3) == 3) continue;
        Inflated inf;
        if (!try_stream(d + i + 2, n - i - 2, inf, min_comp)) continue;
        size_t e = i + 2 + inf.comp_len; if (e + 4 > n) continue;
        if (adler32(inf.raw.data(), inf.raw.size()) != be32(d + e)) continue;
        bool ov = false; for (size_t k = i; k < e + 4; k++) if (covered[k]) { ov = true; break; }
        if (ov) continue;
        Region r; r.kind = K_ZLIB; r.start = i; r.end = e + 4; r.zhdr[0] = c; r.zhdr[1] = g; r.comp.assign(d + i + 2, d + e); r.inf = std::move(inf);
        pdf_image_dict(d, n, i, r);
        take(r);
        i = e + 3;
    }
    std::sort(regs.begin(), regs.end(), [](const Region& a, const Region& b) { return a.start < b.start; });
    if (verbose) fprintf(stderr, "scan: %zu streams\n", regs.size());
    return regs;
}

// ---------------------------------------------------------------- container
// HDEF format: "HDF1" | records...  record: 0 = verbatim (varint len, bytes); 1 = stream (kind, varint orig_len,
// varint raw_len, varint rec_len, rec, kind extras, raw bytes)
static const char MAGIC[4] = {'H', 'D', 'F', '1'};

struct Stats { size_t streams = 0, exact = 0, comp = 0, raw = 0, rec = 0, failed = 0, corrections = 0; };

// PNG scanline filters (PNG spec section 9): unfilter to pixels, refilter with the recorded types
static int png_channels(int ct) { return ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0; }
static bool png_geometry(const Region& r, size_t& rowbytes, size_t& bpp) {
    int ch = png_channels(r.png_ct); if (!ch || r.png_il != 0 || !r.png_w || !r.png_h) return false;
    if (!(r.png_bd == 1 || r.png_bd == 2 || r.png_bd == 4 || r.png_bd == 8 || r.png_bd == 16)) return false;
    size_t bits = (size_t)ch * r.png_bd; rowbytes = (bits * r.png_w + 7) / 8; bpp = bits >= 8 ? bits / 8 : 1;
    return rowbytes > 0 && r.png_h < (1u << 28) && rowbytes < (1u << 28);
}
static inline uint8_t paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return (pa <= pb && pa <= pc) ? (uint8_t)a : pb <= pc ? (uint8_t)b : (uint8_t)c; }
// in: filtered rows (type byte + rowbytes each). out: pixels. types: per row. returns false on a bad filter type
static bool png_unfilter(const uint8_t* in, size_t h, size_t rb, size_t bpp, std::vector<uint8_t>& out, std::vector<uint8_t>& types) {
    out.assign(h * rb, 0); types.resize(h);
    for (size_t y = 0; y < h; y++) {
        uint8_t t = in[y * (rb + 1)]; const uint8_t* s = in + y * (rb + 1) + 1; uint8_t* o = out.data() + y * rb; const uint8_t* up = y ? o - rb : nullptr;
        types[y] = t; if (t > 4) return false;
        for (size_t x = 0; x < rb; x++) {
            int a = x >= bpp ? o[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
            int v = s[x];
            switch (t) { case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) >> 1; break; case 4: v += paeth(a, b, c); break; default: break; }
            o[x] = (uint8_t)v;
        }
    }
    return true;
}
static void png_refilter(const uint8_t* px, size_t h, size_t rb, size_t bpp, const uint8_t* types, std::vector<uint8_t>& out) {
    out.resize(h * (rb + 1));
    for (size_t y = 0; y < h; y++) {
        uint8_t t = types[y]; const uint8_t* o = px + y * rb; const uint8_t* up = y ? o - rb : nullptr; uint8_t* d = out.data() + y * (rb + 1); d[0] = t; d++;
        for (size_t x = 0; x < rb; x++) {
            int a = x >= bpp ? o[x - bpp] : 0, b = up ? up[x] : 0, c = (up && x >= bpp) ? up[x - bpp] : 0;
            int v = o[x];
            switch (t) { case 1: v -= a; break; case 2: v -= b; break; case 3: v -= (a + b) >> 1; break; case 4: v -= paeth(a, b, c); break; default: break; }
            d[x] = (uint8_t)v;
        }
    }
}
static std::string pnm_header(const Region& r) {
    char b[160];
    if (r.png_ct == 2 && (r.png_bd == 8 || r.png_bd == 16)) snprintf(b, sizeof b, "P6\n%u %u\n%d\n", r.png_w, r.png_h, r.png_bd == 8 ? 255 : 65535);
    else if (r.png_ct == 0 && (r.png_bd == 8 || r.png_bd == 16)) snprintf(b, sizeof b, "P5\n%u %u\n%d\n", r.png_w, r.png_h, r.png_bd == 8 ? 255 : 65535);
    else if ((r.png_ct == 6 || r.png_ct == 4) && (r.png_bd == 8 || r.png_bd == 16)) snprintf(b, sizeof b, "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n", r.png_w, r.png_h, r.png_ct == 6 ? 4 : 2, r.png_bd == 8 ? 255 : 65535, r.png_ct == 6 ? "RGB_ALPHA" : "GRAYSCALE_ALPHA");
    else return "";
    return b;
}
static bool png_unfilter_on = true;   // HD_NOUNFILTER=1 turns it off
static double max_rec_frac = 0.25;   // HD_MAXREC env: largest record accepted, as a fraction of the stream's deflate bytes
static int max_depth = 6;            // HD_DEPTH env: streams inside streams
static Prior g_prior;

// Pack a blob (whole file, or the raw data of a stream) into a sequence of records.
static void pack_blob(const uint8_t* d, size_t n, int depth, int threads, std::vector<uint8_t>& out, Stats& st, FILE* csv, const char* fname) {
    size_t min_comp = 64;
    std::vector<Region> regs = scan(d, n, min_comp, false);
    std::vector<std::vector<uint8_t>> payload(regs.size());
    std::atomic<size_t> next(0);
    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1); if (i >= regs.size()) return;
            Region& r = regs[i];
            r.sr = search_params(r.inf, r.comp.data(), (r.kind == K_ZLIB || r.kind == K_PNG) ? (r.zhdr[0] >> 4) + 8 : 0, kind_name(r.kind), &g_prior);
            if (!r.sr.ok) { r.note = "no params"; continue; }
            if (r.sr.est_bytes > max_rec_frac * r.comp.size()) { r.note = "record too big"; continue; }   // leave the stream as it was
            std::vector<uint8_t> rebuilt; std::string err;
            if (!unpack_any(r.inf.raw.data(), r.inf.raw.size(), r.sr.rec.data(), r.sr.rec.size(), rebuilt, err)) { r.note = "unpack: " + err; continue; }
            if (rebuilt != r.comp) { r.note = "verify mismatch"; continue; }
            r.ok = true;
            Stats sub;
            size_t rb, bpp;
            bool png_like = (r.kind == K_PNG) || (r.kind == K_ZLIB && r.png_w && r.pdf_pred);
            bool plain_img = (r.kind == K_ZLIB && r.png_w && !r.pdf_pred);
            if (png_unfilter_on && png_like && png_geometry(r, rb, bpp) && r.inf.raw.size() == (size_t)r.png_h * (rb + 1)) {
                std::vector<uint8_t> px, types, back;
                if (png_unfilter(r.inf.raw.data(), r.png_h, rb, bpp, px, types)) {
                    png_refilter(px.data(), r.png_h, rb, bpp, types.data(), back);
                    if (back == r.inf.raw) {
                        std::string hdr = pnm_header(r);
                        r.filters = std::move(types); r.pnm_hdr_len = hdr.size(); r.img_mode = 1;
                        payload[i].push_back(0); put_var(payload[i], hdr.size() + px.size());
                        payload[i].insert(payload[i].end(), hdr.begin(), hdr.end()); payload[i].insert(payload[i].end(), px.begin(), px.end());
                    }
                }
            } else if (png_unfilter_on && plain_img && png_geometry(r, rb, bpp) && r.inf.raw.size() == (size_t)r.png_h * rb) {
                std::string hdr = pnm_header(r);
                if (!hdr.empty()) { r.pnm_hdr_len = hdr.size(); r.img_mode = 2;
                    payload[i].push_back(0); put_var(payload[i], hdr.size() + r.inf.raw.size());
                    payload[i].insert(payload[i].end(), hdr.begin(), hdr.end()); payload[i].insert(payload[i].end(), r.inf.raw.begin(), r.inf.raw.end()); }
            }
            if (!payload[i].empty()) {}
            else if (depth + 1 < max_depth) pack_blob(r.inf.raw.data(), r.inf.raw.size(), depth + 1, 1, payload[i], sub, csv, fname);
            else { payload[i].push_back(0); put_var(payload[i], r.inf.raw.size()); payload[i].insert(payload[i].end(), r.inf.raw.begin(), r.inf.raw.end()); }
            std::lock_guard<std::mutex> g(g_prior.mu);   // stats are shared; the prior's mutex doubles as the stats lock
            st.streams += sub.streams; st.exact += sub.exact; st.comp += sub.comp; st.raw += sub.raw; st.rec += sub.rec; st.failed += sub.failed; st.corrections += sub.corrections;
        }
    };
    std::vector<std::thread> th; for (int t = 0; t < threads; t++) th.emplace_back(worker); for (auto& t : th) t.join();
    size_t pos = 0;
    auto verbatim = [&](size_t a, size_t b) { if (b <= a) return; out.push_back(0); put_var(out, b - a); out.insert(out.end(), d + a, d + b); };
    for (size_t i = 0; i < regs.size(); i++) {
        Region& r = regs[i];
        st.streams++; st.comp += r.comp.size(); st.raw += r.inf.raw.size();
        if (csv) fprintf(csv, "%s,%s%d,%zu,%zu,%zu,%zu,%zu,%d,%s,%s\n", fname, kind_name(r.kind), depth, r.start, r.comp.size(), r.inf.raw.size(), r.ok ? r.sr.rec.size() : 0, r.ok ? r.sr.corrections : 0, r.ok ? r.sr.tried : 0, r.ok ? r.sr.name().c_str() : "-", r.ok ? (r.sr.exact ? "exact" : (r.sr.rank ? "rank" : "diff")) : r.note.c_str());
        if (!r.ok) { st.failed++; continue; }
        st.rec += r.sr.rec.size(); st.corrections += r.sr.corrections; if (r.sr.exact) st.exact++;
        verbatim(pos, r.start);
        out.push_back(1); out.push_back(r.kind); put_var(out, r.end - r.start); put_var(out, r.inf.raw.size()); put_var(out, r.sr.rec.size());
        out.insert(out.end(), r.sr.rec.begin(), r.sr.rec.end());
        if (r.kind == K_ZLIB || r.kind == K_PNG) {
            uint16_t h = (uint16_t)((r.zhdr[0] << 8) | r.zhdr[1]); int idx = -1;
            for (size_t k = 0; k < sizeof(common_zhdr) / 2; k++) if (common_zhdr[k] == h) { idx = (int)k; break; }
            if (idx >= 0) out.push_back((uint8_t)idx); else { out.push_back(255); out.push_back(r.zhdr[0]); out.push_back(r.zhdr[1]); }
        }
        if (r.kind == K_PNG) { put_var(out, r.png_chunks.size()); for (uint32_t c : r.png_chunks) put_var(out, c); }
        if (r.kind == K_PNG || r.kind == K_ZLIB) {
            out.push_back((uint8_t)r.img_mode);
            if (r.img_mode) { put_var(out, r.png_w); put_var(out, r.png_h); out.push_back((uint8_t)r.png_bd); out.push_back((uint8_t)r.png_ct); put_var(out, r.pnm_hdr_len); }
            if (r.img_mode == 1) out.insert(out.end(), r.filters.begin(), r.filters.end());
        }
        put_var(out, payload[i].size()); out.insert(out.end(), payload[i].begin(), payload[i].end());
        pos = r.end;
    }
    verbatim(pos, n);
}

static void pack_file(const std::vector<uint8_t>& f, std::vector<uint8_t>& out, int threads, Stats& st, FILE* csv, const char* fname, bool verbose) {
    if (getenv("HD_MAXREC")) max_rec_frac = atof(getenv("HD_MAXREC"));
    if (getenv("HD_DEPTH")) max_depth = atoi(getenv("HD_DEPTH"));
    if (getenv("HD_NOUNFILTER")) png_unfilter_on = false;
    (void)verbose;
    out.clear(); out.insert(out.end(), MAGIC, MAGIC + 4);
    pack_blob(f.data(), f.size(), 0, threads, out, st, csv, fname);
}

struct Item { int type; size_t a, b; uint8_t kind; size_t orig_len; size_t raw_len; const uint8_t* rec; size_t rec_len; uint8_t zhdr[2]; std::vector<uint32_t> chunks; int png_mode = 0; uint32_t png_w = 0, png_h = 0; int png_bd = 0, png_ct = 0; size_t pnm_hdr_len = 0; std::vector<uint8_t> filters; const uint8_t* payload; size_t payload_len; std::vector<uint8_t> built; bool ok = false; std::string e; };

static bool parse_blob(const uint8_t* in, size_t n, std::vector<Item>& items, std::string& err) {
    try {
        VarReader r(in, n);
        while (!r.done()) {
            Item it; it.type = r.byte();
            if (it.type == 0) { size_t len = r.get(); it.a = r.i; it.b = r.i + len; if (it.b > n) throw std::runtime_error("truncated"); r.i = it.b; items.push_back(std::move(it)); continue; }
            if (it.type != 1) throw std::runtime_error("bad record");
            it.kind = r.byte(); it.orig_len = r.get(); it.raw_len = r.get(); it.rec_len = r.get();
            it.rec = in + r.i; r.i += it.rec_len; if (r.i > n) throw std::runtime_error("truncated");
            if (it.kind == K_ZLIB || it.kind == K_PNG) { uint8_t c = r.byte(); if (c == 255) { it.zhdr[0] = r.byte(); it.zhdr[1] = r.byte(); } else { if (c >= sizeof(common_zhdr) / 2) throw std::runtime_error("bad zhdr"); it.zhdr[0] = common_zhdr[c] >> 8; it.zhdr[1] = common_zhdr[c] & 255; } }
            if (it.kind == K_PNG) { size_t nc = r.get(); for (size_t k = 0; k < nc; k++) it.chunks.push_back((uint32_t)r.get()); }
            if (it.kind == K_PNG || it.kind == K_ZLIB) {
                it.png_mode = r.byte();
                if (it.png_mode) { it.png_w = (uint32_t)r.get(); it.png_h = (uint32_t)r.get(); it.png_bd = r.byte(); it.png_ct = r.byte(); it.pnm_hdr_len = r.get(); }
                if (it.png_mode == 1) { it.filters.resize(it.png_h); for (size_t k = 0; k < it.png_h; k++) it.filters[k] = r.byte(); }
                else if (it.png_mode > 2) throw std::runtime_error("bad image mode"); }
            it.payload_len = r.get(); it.payload = in + r.i; r.i += it.payload_len; if (r.i > n) throw std::runtime_error("truncated");
            items.push_back(std::move(it));
        }
    } catch (std::exception& e) { err = e.what(); return false; }
    return true;
}

static bool unpack_blob(const uint8_t* in, size_t n, int threads, std::vector<uint8_t>& out, std::string& err);

static bool build_item(Item& it, int threads) {
    std::vector<uint8_t> raw;
    if (!unpack_blob(it.payload, it.payload_len, threads, raw, it.e)) return false;
    if (it.png_mode == 1) {
        Region g; g.png_w = it.png_w; g.png_h = it.png_h; g.png_bd = it.png_bd; g.png_ct = it.png_ct; g.png_il = 0; size_t rb, bpp;
        if (!png_geometry(g, rb, bpp) || raw.size() != it.pnm_hdr_len + (size_t)it.png_h * rb) { it.e = "png pixel size"; return false; }
        std::vector<uint8_t> back; png_refilter(raw.data() + it.pnm_hdr_len, it.png_h, rb, bpp, it.filters.data(), back); raw.swap(back);
    } else if (it.png_mode == 2) {
        if (raw.size() < it.pnm_hdr_len) { it.e = "image header"; return false; }
        raw.erase(raw.begin(), raw.begin() + it.pnm_hdr_len);
    }
    if (raw.size() != it.raw_len) { it.e = "raw length mismatch"; return false; }
    std::vector<uint8_t> comp;
    if (!unpack_any(raw.data(), raw.size(), it.rec, it.rec_len, comp, it.e)) return false;
    std::vector<uint8_t>& b = it.built;
    if (it.kind == K_RAW || it.kind == K_GZIP) b = std::move(comp);
    else if (it.kind == K_ZLIB) { b.push_back(it.zhdr[0]); b.push_back(it.zhdr[1]); b.insert(b.end(), comp.begin(), comp.end()); uint32_t a = adler32(raw.data(), raw.size()); b.push_back(a >> 24); b.push_back(a >> 16); b.push_back(a >> 8); b.push_back(a); }
    else if (it.kind == K_PNG) {
        std::vector<uint8_t> z; z.push_back(it.zhdr[0]); z.push_back(it.zhdr[1]); z.insert(z.end(), comp.begin(), comp.end()); uint32_t a = adler32(raw.data(), raw.size()); z.push_back(a >> 24); z.push_back(a >> 16); z.push_back(a >> 8); z.push_back(a);
        size_t p = 0;
        for (uint32_t len : it.chunks) {
            if (p + len > z.size()) { it.e = "png chunk overrun"; return false; }
            b.push_back(len >> 24); b.push_back(len >> 16); b.push_back(len >> 8); b.push_back(len);
            size_t cs = b.size(); b.insert(b.end(), {'I', 'D', 'A', 'T'}); b.insert(b.end(), z.begin() + p, z.begin() + p + len);
            uint32_t c = crc32buf(0, b.data() + cs, 4 + len); b.push_back(c >> 24); b.push_back(c >> 16); b.push_back(c >> 8); b.push_back(c);
            p += len;
        }
        if (p != z.size()) { it.e = "png chunk sizes"; return false; }
    }
    if (b.size() != it.orig_len) { it.e = "length mismatch"; return false; }
    it.ok = true; return true;
}

static bool unpack_blob(const uint8_t* in, size_t n, int threads, std::vector<uint8_t>& out, std::string& err) {
    std::vector<Item> items;
    if (!parse_blob(in, n, items, err)) return false;
    if (threads > 1) {
        std::atomic<size_t> next(0);
        auto worker = [&]() { for (;;) { size_t i = next.fetch_add(1); if (i >= items.size()) return; if (items[i].type == 1) build_item(items[i], 1); } };
        std::vector<std::thread> th; for (int t = 0; t < threads; t++) th.emplace_back(worker); for (auto& t : th) t.join();
    } else for (Item& it : items) if (it.type == 1) build_item(it, 1);
    out.clear();
    for (Item& it : items) {
        if (it.type == 0) { out.insert(out.end(), in + it.a, in + it.b); continue; }
        if (!it.ok) { err = "stream rebuild failed: " + it.e; return false; }
        out.insert(out.end(), it.built.begin(), it.built.end());
    }
    return true;
}

static bool unpack_file(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, int threads, std::string& err) {
    if (in.size() < 4 || memcmp(in.data(), MAGIC, 4)) { err = "not an HDEF file"; return false; }
    return unpack_blob(in.data() + 4, in.size() - 4, threads, out, err);
}

#ifdef HAVE_ZLIB
static int zcheck(const char* fn) {
    std::vector<uint8_t> f = read_file(fn);
    int bad = 0, total = 0;
    for (int level = 0; level <= 9; level++) for (int mem : {8, 9, 1, 2, 5}) for (int strat : {0, 1, 4, 3, 2}) for (int wb : {15, 9, 12}) {
        if (level == 0 && (strat || mem != 8)) continue;
        z_stream zs; memset(&zs, 0, sizeof zs);
        if (deflateInit2(&zs, level, Z_DEFLATED, -wb, mem, strat) != Z_OK) continue;
        std::vector<uint8_t> comp(deflateBound(&zs, f.size()) + 64);
        zs.next_in = (Bytef*)f.data(); zs.avail_in = (uInt)f.size(); zs.next_out = comp.data(); zs.avail_out = (uInt)comp.size();
        int rc = deflate(&zs, Z_FINISH); if (rc != Z_STREAM_END) { fprintf(stderr, "deflate rc %d\n", rc); deflateEnd(&zs); continue; }
        comp.resize(zs.total_out); deflateEnd(&zs);
        total++;
        Inflated inf;
        if (!inflate_record(comp.data(), comp.size(), inf) || inf.comp_len != comp.size() || inf.raw != f) { printf("L%d M%d S%d W%d: inflate failed (%s)\n", level, mem, strat, wb, inf.error.c_str()); bad++; continue; }
        Params P = zlib_params(level, mem, wb, strat);
        PackResult pr = pack_stream(inf, comp.data(), P);
        std::vector<uint8_t> rebuilt; std::string err;
        bool ok = pr.ok && unpack_stream(f.data(), f.size(), pr.rec.data(), pr.rec.size(), rebuilt, err) && rebuilt == comp;
        if (!ok || pr.corrections || pr.rec.size() > 20 + 4 * inf.blocks.size()) {
            printf("L%d M%d S%d W%d: %s corr=%zu rec=%zu blocks=%zu comp=%zu %s\n", level, mem, strat, wb, ok ? "ok" : "MISMATCH", pr.corrections, pr.rec.size(), inf.blocks.size(), comp.size(), err.c_str());
            if (!ok || pr.corrections) bad++;
        }
    }
    printf("%s: %d of %d settings not bit-exact\n", fn, bad, total);
    return bad != 0;
}
#endif

int main(int argc, char** argv) {
    crc_init();
    if (argc < 3) { fprintf(stderr, "usage: hdeflate pack|unpack IN OUT [threads] | test IN [threads] [stats.csv] | zcheck FILE\n"); return 2; }
    std::string mode = argv[1];
#ifdef HAVE_ZLIB
    if (mode == "zcheck") return zcheck(argv[2]);
#endif
    if (mode == "pack" || mode == "test") {
        int threads = argc > (mode == "pack" ? 4 : 3) ? atoi(argv[mode == "pack" ? 4 : 3]) : 2;
        std::vector<uint8_t> f = read_file(argv[2]);
        std::vector<uint8_t> out; Stats st;
        FILE* csv = nullptr;
        if (mode == "test" && argc > 4) { csv = fopen(argv[4], "a"); }
        auto t0 = std::chrono::steady_clock::now();
        pack_file(f, out, threads, st, csv, argv[2], mode == "test");
        double tp = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (csv) fclose(csv);
        if (mode == "pack") { write_file(argv[3], out); fprintf(stderr, "%zu -> %zu bytes, %zu streams (%zu exact, %zu failed), rec %zu bytes, %.2fs\n", f.size(), out.size(), st.streams, st.exact, st.failed, st.rec, tp); return 0; }
        std::vector<uint8_t> back; std::string err;
        t0 = std::chrono::steady_clock::now();
        bool ok = unpack_file(out, back, threads, err);
        double tu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool same = ok && back == f;
        printf("%s: %zu -> %zu bytes | streams %zu exact %zu failed %zu | comp %zu raw %zu rec %zu corr %zu | pack %.2fs unpack %.2fs | %s%s\n",
               argv[2], f.size(), out.size(), st.streams, st.exact, st.failed, st.comp, st.raw, st.rec, st.corrections, tp, tu, same ? "EXACT" : "MISMATCH ", same ? "" : err.c_str());
        return same ? 0 : 1;
    }
    if (mode == "unpack") {
        int threads = argc > 4 ? atoi(argv[4]) : 2;
        std::vector<uint8_t> f = read_file(argv[2]); std::vector<uint8_t> out; std::string err;
        if (!unpack_file(f, out, threads, err)) { fprintf(stderr, "unpack failed: %s\n", err.c_str()); return 1; }
        write_file(argv[3], out); return 0;
    }
    fprintf(stderr, "unknown mode\n"); return 2;
}
