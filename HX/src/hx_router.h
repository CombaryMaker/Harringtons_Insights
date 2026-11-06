// hx_router.h - HX Complete: new routes (HCM 3.7 + EXE, 16-bit raw image -> JPEG XL) and the learned router:
// predict the best three routes, run them, escalate when the result misses the prediction (3% -> next three,
// 10% -> all), and log every file (features + every route tried + misses + surprises) so the router keeps learning.
// Michael Hugh Harrington's design (best-of-three with escalation keyed to the gap between predicted and actual size).
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. All rights reserved - NOT MIT. See HX/LICENSE.txt.
#pragma once
#include <map>
#include <sstream>
#include <fstream>

// ---------- mode 17: HCM 3.7 (MIT + kanzi parts Apache 2.0), optional kanzi EXE filter (kzf) in front ----------
static B hcm37_route(const B &in, bool exe) {
    const char *h = getenv("HX_HCM37"), *k = getenv("HX_KZF"); if (!h || (exe && !k)) return B();
    string d = mz_tmp(), a = d + "/a", ak = d + "/a.k", b = d + "/b", c = d + "/c", ck = d + "/c.k";
    auto cl = [&]() { for (auto &x : {a, ak, b, c, ck}) remove(x.c_str()); rmdir(d.c_str()); };
    writef(a.c_str(), in.data(), in.size()); string src = a;
    if (exe) { if (!sh(q(k) + " f EXE " + q(a) + " " + q(ak))) { cl(); return B(); } src = ak; }
    const char *op = in.size() > ((size_t)24 << 20) ? "t" : "x";
    if (!sh(q(h) + " " + op + " " + q(src) + " " + q(b))) { cl(); return B(); }
    // verified round trip
    bool ok = sh(q(h) + " d " + q(b) + " " + q(exe ? ck : c)) && (!exe || sh(q(k) + " i " + q(ck) + " " + q(c)));
    B body; if (ok && readf(c.c_str()) == in) { body.push_back(exe ? 1 : 0); B z = readf(b.c_str()); body.insert(body.end(), z.begin(), z.end()); }
    cl(); return body;
}
static B hcm37_unroute(const uint8_t *p, size_t m) {
    const char *h = getenv("HX_HCM37"), *k = getenv("HX_KZF"); bool exe = p[0] == 1; if (!h || (exe && !k)) return B();
    string d = mz_tmp(), b = d + "/b", c = d + "/c", ck = d + "/c.k"; writef(b.c_str(), p + 1, m - 1); B out;
    if (sh(q(h) + " d " + q(b) + " " + q(exe ? ck : c)) && (!exe || sh(q(k) + " i " + q(ck) + " " + q(c)))) out = readf(c.c_str());
    for (auto &x : {b, c, ck}) remove(x.c_str()); rmdir(d.c_str()); return out;
}

// ---------- mode 18: raw 16-bit greyscale image (DICOM pixel data or bare rows) -> JPEG XL lossless ----------
// body: [zlen 4][zstd(header) ][hlen 4][tlen 4][w 4][rows 4][big-endian 1][bits 1] + tail stored inside the zstd block + JXL
static size_t dicom_pixels(const B &in, size_t &len) {   // offset of the (7FE0,0010) pixel data, or 0
    size_t lim = min<size_t>(in.size(), 1 << 20);
    for (size_t i = 128; i + 12 < lim; i++) if (in[i] == 0xE0 && in[i + 1] == 0x7F && in[i + 2] == 0x10 && in[i + 3] == 0x00) {
        if ((in[i + 4] == 'O' && (in[i + 5] == 'W' || in[i + 5] == 'B'))) { len = get32(&in[i + 8]); return i + 12; }
        len = get32(&in[i + 4]); if (len > 0 && i + 8 + len <= in.size()) return i + 8; }
    return 0;
}
static B img16_route(const B &in, size_t w, bool dbg) {
    size_t n = in.size(); if (w < 16 || !getenv("HX_CJXL") || !getenv("HX_DJXL")) return B();
    size_t h = 0, len = 0, off = dicom_pixels(in, len);
    if (off && len != 0xFFFFFFFF && off + len <= n && len % (2 * w) == 0) h = off; else { h = n % (2 * w); len = n - h; }
    size_t rows = len / (2 * w); if (rows < 32) return B(); size_t tl = n - h - rows * 2 * w;
    const uint8_t *px = in.data() + h; double le = 0, be = 0; unsigned mx = 0;
    for (size_t r = 0; r < rows; r += max<size_t>(1, rows / 64)) for (size_t x = 1; x < w; x++) {
        size_t i = (r * w + x) * 2; int a = px[i] | px[i + 1] << 8, b = px[i - 2] | px[i - 1] << 8; le += abs(a - b);
        int c = px[i] << 8 | px[i + 1], e = px[i - 2] << 8 | px[i - 1]; be += abs(c - e); }
    bool big = be < le;
    for (size_t i = 0; i < rows * w; i++) { unsigned v = big ? (px[2 * i] << 8 | px[2 * i + 1]) : (px[2 * i] | px[2 * i + 1] << 8); mx = max(mx, v); }
    int bits = 1; while (bits < 16 && (1u << bits) <= mx) bits++; if (bits <= 8) return B();   // 8-bit images go to the normal image routes
    string d = mz_tmp(), a = d + "/a.pgm", b = d + "/b.jxl", c = d + "/c.pgm";
    string hd = "P5\n" + to_string(w) + " " + to_string(rows) + "\n" + to_string((1 << bits) - 1) + "\n";
    B pg(hd.begin(), hd.end()); pg.reserve(pg.size() + rows * w * 2);
    for (size_t i = 0; i < rows * w; i++) { if (big) { pg.push_back(px[2 * i]); pg.push_back(px[2 * i + 1]); } else { pg.push_back(px[2 * i + 1]); pg.push_back(px[2 * i]); } }
    writef(a.c_str(), pg.data(), pg.size()); B best;
    for (const char *opt : {"-d 0 -e 9", "-d 0 -e 7 -P 15 -g 1 -I 100"}) {   // stock effort 9, and the jxl-hi settings
        if (!sh(q(tool("HX_CJXL")) + " " + q(a) + " " + q(b) + " " + opt + " --quiet")) continue; B j = readf(b.c_str());
        if (!sh(q(tool("HX_DJXL")) + " " + q(b) + " " + q(c) + " --quiet")) continue; B r = readf(c.c_str());
        if (r.size() < rows * w * 2 || memcmp(r.data() + r.size() - rows * w * 2, pg.data() + pg.size() - rows * w * 2, rows * w * 2)) continue;
        if (best.empty() || j.size() < best.size()) best.swap(j); }
    remove(a.c_str()); remove(b.c_str()); remove(c.c_str()); rmdir(d.c_str()); if (best.empty()) return B();
    B side(in.begin(), in.begin() + h); side.insert(side.end(), in.end() - tl, in.end());
    B z(ZSTD_compressBound(side.size()) + 16); size_t zl = side.empty() ? 0 : ZSTD_compress(z.data(), z.size(), side.data(), side.size(), 19); if (ZSTD_isError(zl)) return B(); z.resize(zl);
    B body; put32(body, (uint32_t)zl); body.insert(body.end(), z.begin(), z.end()); put32(body, (uint32_t)h); put32(body, (uint32_t)tl); put32(body, (uint32_t)w); put32(body, (uint32_t)rows);
    body.push_back(big); body.push_back((uint8_t)bits); body.insert(body.end(), best.begin(), best.end());
    if (dbg) fprintf(stderr, "  img16: header %zu, %zu x %zu, %d bits, %s -> %zu\n", h, w, rows, bits, big ? "BE" : "LE", body.size());
    return body;
}
static B img16_unroute(const uint8_t *p, size_t m, size_t n) {
    size_t zl = get32(p); const uint8_t *z = p + 4; const uint8_t *q2 = z + zl; size_t h = get32(q2), tl = get32(q2 + 4), w = get32(q2 + 8), rows = get32(q2 + 12); bool big = q2[16]; const uint8_t *j = q2 + 18;
    size_t jl = m - (j - p); B side(h + tl); if (h + tl) { size_t r = ZSTD_decompress(side.data(), side.size(), z, zl); if (ZSTD_isError(r) || r != h + tl) return B(); }
    string d = mz_tmp(), b = d + "/b.jxl", c = d + "/c.pgm"; writef(b.c_str(), j, jl); B out;
    if (sh(q(tool("HX_DJXL")) + " " + q(b) + " " + q(c) + " --quiet")) { B r = readf(c.c_str()); size_t pxn = rows * w * 2;
        if (r.size() >= pxn) { const uint8_t *s = r.data() + r.size() - pxn; out.assign(side.begin(), side.begin() + h); out.resize(h + pxn);
            for (size_t i = 0; i < rows * w; i++) { if (big) { out[h + 2 * i] = s[2 * i]; out[h + 2 * i + 1] = s[2 * i + 1]; } else { out[h + 2 * i] = s[2 * i + 1]; out[h + 2 * i + 1] = s[2 * i]; } }
            out.insert(out.end(), side.begin() + h, side.end()); } }
    remove(b.c_str()); remove(c.c_str()); rmdir(d.c_str()); if (out.size() != n) return B(); return out;
}

// ---------- the learned router ----------
static const int NF = 17;
static vector<double> hx15_features(const B &in, size_t base_best, double lines, double z19s, double z1s, size_t x86cv, int fmt) {
    size_t n = in.size(); B s = sample_of(in, (size_t)1 << 20); double hist[256] = {0}; for (uint8_t c : s) hist[c]++;
    double H = 0, pr = 0, hi = 0, zr = 0, ns = max<size_t>(1, s.size());
    for (int c = 0; c < 256; c++) { double p = hist[c] / ns; if (p > 0) H -= p * log2(p); if ((c >= 32 && c < 127) || c == 9 || c == 10 || c == 13) pr += p; if (c >= 128) hi += p; }
    zr = hist[0] / ns; B bs = bsc_enc(s.data(), s.size()); double bsr = bs.empty() ? 1.0 : (double)bs.size() / ns;
    vector<double> f = {log2((double)max<size_t>(n, 1)), (double)base_best / n, lines / n, z19s / n, z1s / n, H / 8, pr, hi, zr,
        g_w16 ? 1.0 : 0.0, g_w8 ? 1.0 : 0.0, g_rec ? 1.0 : 0.0, min(10.0, x86cv * 1000.0 / n), bsr, fmt == 1 ? 1.0 : 0.0, fmt == 2 ? 1.0 : 0.0, fmt >= 3 ? 1.0 : 0.0};
    return f;
}
struct Router {
    struct Row { vector<double> f; map<int, double> lr; string set; };   // lr = log(route size / base best)
    vector<Row> rows; vector<double> mu, sd; double novel_thr = 1e300, last_nn = 0;
    bool load(const string &path, const string &skip_set) {
        ifstream is(path); string line; while (getline(is, line)) { if (line.empty() || line[0] == '#') continue;
            vector<string> c; { stringstream ss(line); string t; while (getline(ss, t, ',')) c.push_back(t); }
            if (c.size() < (size_t)(4 + NF + 6)) continue; Row r; r.set = c[0]; for (int i = 0; i < NF; i++) r.f.push_back(atof(c[3 + i].c_str()));
            double bb = atof(c[3 + NF].c_str()); if (bb <= 0) continue;
            stringstream rs(c.back()); string e; while (getline(rs, e, '|')) { int id; double sz, sec; if (sscanf(e.c_str(), "%d:%lf:%lf", &id, &sz, &sec) == 3 && sz > 0) r.lr[id] = log(sz / bb); }
            if (!skip_set.empty() && r.set == skip_set) continue; rows.push_back(r); }
        if (rows.size() < 20) return false;
        mu.assign(NF, 0); sd.assign(NF, 0); for (auto &r : rows) for (int i = 0; i < NF; i++) mu[i] += r.f[i]; for (int i = 0; i < NF; i++) mu[i] /= rows.size();
        for (auto &r : rows) for (int i = 0; i < NF; i++) sd[i] += (r.f[i] - mu[i]) * (r.f[i] - mu[i]); for (int i = 0; i < NF; i++) sd[i] = sqrt(sd[i] / rows.size()) + 1e-6;
        // novelty threshold: the 90th percentile of each training file's distance to its nearest other training file
        vector<double> nn; for (size_t i = 0; i < rows.size(); i++) { double b = 1e300; for (size_t j = 0; j < rows.size(); j++) if (i != j) { double d = 0; for (int k = 0; k < NF; k++) { double t = (rows[i].f[k] - rows[j].f[k]) / sd[k]; d += t * t; } b = min(b, sqrt(d)); } nn.push_back(b); }
        sort(nn.begin(), nn.end()); novel_thr = nn[(size_t)(0.9 * (nn.size() - 1))];
        return true; }
    // predicted ratio (route size / base best) per route; unknown routes get 1.02 so they rank behind known good ones
    vector<pair<double, int>> predict(const vector<double> &f, const vector<int> &app, int K = 7) {
        vector<pair<double, int>> dist; for (size_t i = 0; i < rows.size(); i++) { double d = 0; for (int j = 0; j < NF; j++) { double t = (f[j] - rows[i].f[j]) / sd[j]; d += t * t; } dist.push_back({sqrt(d), (int)i}); }
        sort(dist.begin(), dist.end()); last_nn = dist.empty() ? 0 : dist[0].first; vector<pair<double, int>> out;
        for (int id : app) { double ws = 0, s = 0; int k = 0; for (auto &dd : dist) { auto it = rows[dd.second].lr.find(id); if (it == rows[dd.second].lr.end()) continue;
                double wt = 1.0 / (0.05 + dd.first); s += wt * it->second; ws += wt; if (++k >= K) break; }
            out.push_back({ws > 0 ? exp(s / ws) : 1.02, id}); }
        // rule: a detected format's specialist (10 = media, 18 = 16-bit image) always runs in the first pass
        stable_sort(out.begin(), out.end(), [](const pair<double, int> &a, const pair<double, int> &b) { int sa = a.second == 10 || a.second == 18, sb = b.second == 10 || b.second == 18; return sa != sb ? sa > sb : a.first < b.first; });
        return out; }
};
