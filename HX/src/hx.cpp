// hx.cpp - HX: HX Fast (default) and HX Complete (--complete), complete compressor/decompressor, Nov 2026.
// Patent Pending (U.S. Provisional App. 64/192,843). Copyright (c) 2026 Michael Hugh Harrington. Not MIT: free for personal use and non-commercial research/peer review; commercial use needs a license. See HX/LICENSE.txt.
// Harrington's Sets (64 KB windows into up to 4 classes, kept only when smaller) + Harrington Exchange lines (byte
// lanes, 12-bit samples, context lines) feeding the standard entropy coders Huff0 / FSE (Yann Collet, BSD) and rANS
// (ryg_rans, Fabian Giesen, public domain); or, when the file is made of long repeats, the cascade's repeat line
// (zstd, Meta, BSD, level 19, 128 MB window, long-distance matching). The smaller one is kept; raw if neither shrinks.
// Every segment decodes on its own core.
// HX Fast 8 (3 Nov 2026): same file format and decoder as HX Fast 7; faster compression - each line is first tried on a
// 128 KB probe of the segment and only the best few are run in full; the Sets passes only try the lines that won without
// Sets; the split mode reuses the segments already made; spare cores work on the lines of one segment. --full = old search.
// HX COMPLETE (--complete, 3 Nov 2026): everything HX Fast tries, plus the two strongest fast block-sorters - BSC
// (Ilya Grebnov, Apache 2.0) and bzip3 (Kamila Szewczyk, LGPL) - on the whole file, and the split with BSC or bzip3
// as the coder for the parts the Exchange lines do not win. The smallest wins (no speed preference).
// RESEARCH ONLY (Michael, 3 Nov 2026: "we can use it for research; our final form will not have it"): with the
// environment variable HX_MZIP set to the mzip program (Cranot/mzip, AGPL - run as a separate program, never linked
// in), --complete also tries mzip on the whole file (mode 8) and as the coder of the split's rest (mode 9).
// Files made with modes 8/9 need HX_MZIP to decode too.
// usage (HX 1.1.0): hx c IN OUT [--complete] [--block=SIZE] [--memlimit=SIZE] [-T N]  |  hx d IN OUT [-T N]   (IN/OUT can be -)
#define HX_NO_MAIN
#include "entbench.cpp"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "libbsc.h"
#include "libbz3.h"
#include "x86f.h"
#include <functional>
#include <unistd.h>
#include "hx_win_compat.h"

static const size_t SEG = (1u << 20) - (1u << 20) % 12, HBLK = 128 * 1024;
// HX Ablation Test switches (8 Nov 2026, encoder only - every file still decodes with the normal "hx d"):
//   --abl-pick1        H Sampler replaced by "sample picks the winner": the probe alone chooses one line, no full-run final
//   --full             H Sampler replaced by "try every line in full"
//   --abl-lines-off    Exchange lines replaced by plain order-0 entropy coding of the bytes (Huff0 / FSE / rANS)
//   --abl-sets-off     Harrington's Sets replaced by plain fixed segments
//   --abl-gmap FILE    Harrington's Sets classes replaced by an outside grouping map (GMP1 file, 64 KB windows, up to 4 classes)
//   --abl-repeat-off   the cascade's repeat line replaced by nothing (leftovers stay with the segment coders / stored raw)
//   --abl-split-off    the split route replaced by whole-file best-of only
static bool abl_lines_off = false, abl_sets_off = false, abl_repeat_off = false, abl_split_off = false; static string abl_gmap;
static const vector<int> abl_plain = {0, 32, 128};
struct Plan { bool sets = false; size_t sw = 65536; vector<int> lab; vector<size_t> worder, soff, slen; };
static void plan_segments(Plan &P, size_t n) {   // segments of the (possibly class-sorted) stream, from the class map
    P.worder.clear(); P.soff.clear(); P.slen.clear();
    if (!P.sets) { for (size_t q = 0; q < n; q += SEG) { P.soff.push_back(q); P.slen.push_back(min(SEG, n - q)); } return; }
    size_t nw = P.lab.size(), pos = 0;
    for (int k = 0; k < 4; k++) { size_t c0 = pos;
        for (size_t w = 0; w < nw; w++) if (P.lab[w] == k) { P.worder.push_back(w); pos += min(P.sw, n - w * P.sw); }
        for (size_t q = c0; q < pos; q += SEG) { P.soff.push_back(q); P.slen.push_back(min(SEG, pos - q)); } }
}
static void runpar(size_t cnt, int T, const function<void(size_t)> &f) {
    vector<thread> th; for (int t = 0; t < T; t++) th.emplace_back([&, t]() { for (size_t k = cnt * t / T; k < cnt * (t + 1) / T; k++) f(k); });
    for (auto &x : th) x.join();
}
static B hx_encode(const B &in, const Plan &P, int T, const vector<int> *allow = nullptr, vector<int> *chosen = nullptr, vector<B> *parts = nullptr, vector<int> *keptall = nullptr) {
    size_t n = in.size(); B perm; const uint8_t *src = in.data();
    if (P.sets) { perm.resize(n); size_t pos = 0; for (size_t w : P.worder) { size_t L = min(P.sw, n - w * P.sw); memcpy(perm.data() + pos, in.data() + w * P.sw, L); pos += L; } src = perm.data(); }
    vector<B> part(P.soff.size()); vector<int> ch(part.size(), 0); int TS = (int)min<size_t>(T, max<size_t>(1, part.size())); g_ct = max(1, T / TS);
    vector<vector<int>> kp(part.size());
    runpar(part.size(), TS, [&](size_t k) { ch[k] = hx_seg_enc(src + P.soff[k], P.slen[k], HBLK, part[k], allow, &kp[k]); });
    if (chosen) *chosen = ch; if (parts) *parts = part; if (keptall) for (auto &v : kp) keptall->insert(keptall->end(), v.begin(), v.end());
    B o; o.push_back(P.sets);
    if (P.sets) { put32(o, (uint32_t)P.sw); B map((P.lab.size() * 2 + 7) / 8, 0); for (size_t w = 0; w < P.lab.size(); w++) map[w / 4] |= P.lab[w] << (2 * (w % 4)); o.insert(o.end(), map.begin(), map.end()); }
    put32(o, (uint32_t)part.size()); for (auto &q : part) put32(o, (uint32_t)q.size()); for (auto &q : part) o.insert(o.end(), q.begin(), q.end());
    return o;
}
static void hx_decode(const uint8_t *p, size_t m, uint8_t *out, size_t n, int T) {
    Plan P; P.sets = *p++;
    if (P.sets) { P.sw = get32(p); p += 4; size_t nw = (n + P.sw - 1) / P.sw; P.lab.resize(nw); for (size_t w = 0; w < nw; w++) P.lab[w] = (p[w / 4] >> (2 * (w % 4))) & 3; p += (nw * 2 + 7) / 8; }
    plan_segments(P, n); size_t ns = get32(p); p += 4; if (ns != P.soff.size()) { fprintf(stderr, "hxf: damaged file\n"); exit(1); }
    vector<size_t> po(ns); size_t acc = 0; for (size_t k = 0; k < ns; k++) { po[k] = acc; acc += get32(p + 4 * k); } p += 4 * ns;
    B tmp(P.sets ? n : 0); uint8_t *dst = P.sets ? tmp.data() : out;
    runpar(ns, T, [&](size_t k) { hx_seg_dec(p + po[k], dst + P.soff[k], P.slen[k], HBLK); });
    if (P.sets) { vector<size_t> wpos(P.worder.size()); size_t pos = 0; for (size_t i = 0; i < P.worder.size(); i++) { wpos[i] = pos; pos += min(P.sw, n - P.worder[i] * P.sw); }
        runpar(P.worder.size(), T, [&](size_t i) { size_t w = P.worder[i]; memcpy(out + w * P.sw, tmp.data() + wpos[i], min(P.sw, n - w * P.sw)); }); }
}
// zstd-hi (Michael Harrington's encoder-only zstd settings search, 26 Oct 2026): extra encoder settings, chosen per file on a
// sample; the stream is standard zstd, read by the stock decoder. Config 0 is plain zstd.
struct ZHi { int mml, tlen, split, slog; };
static const ZHi ZHI[] = {{0, -1, 0, 0}, {4, 64, 1, 0}, {0, 999, 0, 12}, {3, -1, 2, 12}, {6, 999, 2, 12}, {4, 999, 1, 0}};
static B zstd_cfg(const uint8_t *d, size_t n, int T, int level, int k) {
    B o(ZSTD_compressBound(n)); ZSTD_CCtx *c = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level); ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1);
    ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, 27); ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, T);
    const ZHi &z = ZHI[k]; if (z.mml) ZSTD_CCtx_setParameter(c, ZSTD_c_minMatch, z.mml); if (z.tlen >= 0) ZSTD_CCtx_setParameter(c, ZSTD_c_targetLength, z.tlen);
    if (z.split) ZSTD_CCtx_setParameter(c, ZSTD_c_useBlockSplitter, z.split); if (z.slog) ZSTD_CCtx_setParameter(c, ZSTD_c_searchLog, z.slog);
    size_t r = ZSTD_compress2(c, o.data(), o.size(), d, n); ZSTD_freeCCtx(c);
    if (ZSTD_isError(r)) return B(); o.resize(r); return o;
}
static B sample_of(const B &in, size_t cap);
static int g_zhi = -1;   // chosen zstd-hi config for this file (-1 = not chosen yet)
static B zstd_long(const B &in, int T, int level = 19) {
    if (level < 19 || getenv("HX_NO_HI")) { B o = zstd_cfg(in.data(), in.size(), T, level, 0); if (o.empty()) { fprintf(stderr, "hxf: zstd error\n"); exit(1); } return o; }
    if (g_zhi < 0) { B s = sample_of(in, (size_t)4 << 20); size_t bs = SIZE_MAX; g_zhi = 0;
        for (int k = 0; k < (int)(sizeof ZHI / sizeof ZHI[0]); k++) { B o = zstd_cfg(s.data(), s.size(), T, level, k); if (!o.empty() && o.size() < bs) { bs = o.size(); g_zhi = k; } } }
    B o = zstd_cfg(in.data(), in.size(), T, level, 0); if (o.empty()) { fprintf(stderr, "hxf: zstd error\n"); exit(1); }
    if (g_zhi > 0) { B h = zstd_cfg(in.data(), in.size(), T, level, g_zhi); if (!h.empty() && h.size() < o.size()) o.swap(h); }   // never worse than plain zstd
    return o;
}
static const size_t BCAP = 256u << 20;   // BSC / bzip3 block size cap (files up to this size are one block)
static B bsc_enc(const uint8_t *d, size_t n) {
    B o; put32(o, (uint32_t)((n + BCAP - 1) / BCAP));
    for (size_t q = 0; q < n; q += BCAP) { int m = (int)min(BCAP, n - q); B t(m + LIBBSC_HEADER_SIZE + 1024);
        int r = bsc_compress(d + q, t.data(), m, LIBBSC_DEFAULT_LZPHASHSIZE, LIBBSC_DEFAULT_LZPMINLEN, LIBBSC_BLOCKSORTER_BWT, LIBBSC_CODER_QLFC_ADAPTIVE, LIBBSC_FEATURE_NONE);
        if (r < 0) { fprintf(stderr, "hxf: bsc error\n"); exit(1); } put32(o, (uint32_t)r); o.insert(o.end(), t.begin(), t.begin() + r); }
    return o;
}
static void bsc_dec(const uint8_t *p, uint8_t *out, size_t n, int T) {
    size_t nb = get32(p); p += 4; vector<const uint8_t *> bp(nb); vector<int> bl(nb);
    for (size_t b = 0; b < nb; b++) { bl[b] = (int)get32(p); p += 4; bp[b] = p; p += bl[b]; }
    runpar(nb, T, [&](size_t b) { size_t q = b * BCAP; int w = (int)min(BCAP, n - q);
        if (bsc_decompress(bp[b], bl[b], out + q, w, LIBBSC_FEATURE_NONE) != LIBBSC_NO_ERROR) { fprintf(stderr, "hxf: bsc decode error\n"); exit(1); } });
}
static B bz3_enc(const uint8_t *d, size_t n) {
    size_t bs = min(BCAP, max<size_t>(n, 65 * 1024)); B o(bz3_bound(n) + 64); size_t on = o.size();
    if (bz3_compress((uint32_t)bs, d, o.data(), n, &on) != BZ3_OK) { fprintf(stderr, "hxf: bzip3 error\n"); exit(1); } o.resize(on); return o;
}
static void bz3_dec(const uint8_t *p, size_t m, uint8_t *out, size_t n) {
    size_t on = n; if (bz3_decompress(p, out, m, &on) != BZ3_OK || on != n) { fprintf(stderr, "hxf: bzip3 decode error\n"); exit(1); }
}
static B readf(const char *fn);
static void writef(const char *fn, const uint8_t *d, size_t n);
static string mz_tmp() { const char *t = getenv("TMPDIR"); string d = string(t ? t : hx_tmpdir()) + "/hxmzXXXXXX"; vector<char> b(d.begin(), d.end()); b.push_back(0);
    if (!mkdtemp(b.data())) { perror("hxf: mkdtemp"); exit(1); } return string(b.data()); }
static B mz_run(const char *op, const uint8_t *d, size_t n) {   // research only: runs the external mzip program
    const char *mz = getenv("HX_MZIP"); if (!mz) { fprintf(stderr, "hxf: this file needs HX_MZIP (research build)\n"); exit(1); }
    string dir = mz_tmp(), a = dir + "/a", b = dir + "/b"; writef(a.c_str(), d, n);
    string cmd = string("\"") + mz + "\" " + op + " \"" + a + "\" \"" + b + "\" > " HX_NULL " 2>&1";
    if (hx_system(cmd.c_str()) != 0) { fprintf(stderr, "hxf: mzip failed\n"); exit(1); }
    B o = readf(b.c_str()); remove(a.c_str()); remove(b.c_str()); rmdir(dir.c_str()); return o;
}
static B readf(const char *fn) { FILE *f = fopen(fn, "rb"); if (!f) { perror(fn); exit(1); } fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET); B d(n); if (n && fread(d.data(), 1, n, f) != n) exit(1); fclose(f); return d; }
static void writef(const char *fn, const uint8_t *d, size_t n) { FILE *f = fopen(fn, "wb"); if (!f) { perror(fn); exit(1); } if (n && fwrite(d, 1, n, f) != n) exit(1); fclose(f); }


// MEDIA ROUTES (3 Nov 2026 evening, Michael: "add it then, also in images, same with jpgs"): HX Complete also tries the
// format specialists on whole files and keeps one only when it is smaller AND its decode was checked byte for byte:
//   raw images (binary PPM/PGM, 8-bit): JPEG XL lossless (libjxl, BSD) and, for colour, WebP lossless (libwebp, BSD);
//   JPEG files: packJPG (LGPL) and JPEG XL's lossless JPEG repack (BSD; gives back the identical JPEG);
//   Y4M raw video: FFV1 (FFmpeg, LGPL) and x264 lossless (GPL - research only, like mzip).
// This build runs them as separate programs (paths from environment variables HX_CJXL, HX_DJXL, HX_CWEBP, HX_DWEBP,
// HX_PACKJPG, HX_FFMPEG). Mode 10: route byte, header bytes kept as they are, the specialist's file.
// Routes: 1 JXL raw image, 2 WebP raw image, 3 packJPG, 4 JXL JPEG repack, 5 FFV1 video, 6 x264 lossless video.
static string tool(const char *env) { const char *v = getenv(env); return v ? v : ""; }
#ifdef _WIN32
static string q(const string &x) { return "\"" + x + "\""; }
#else
static string q(const string &x) { return "'" + x + "'"; }
#endif
static bool sh(const string &cmd) { return hx_system((cmd + " > " HX_NULL " 2>&1").c_str()) == 0; }
static size_t pnm_header(const B &in, int &kind) {   // binary PGM (5) / PPM (6), maxval 255: returns header length, 0 = not one
    kind = 0; if (in.size() < 16 || in[0] != 'P' || (in[1] != '5' && in[1] != '6')) return 0;
    size_t i = 2; int vals[3], k = 0;
    while (k < 3 && i < in.size() && i < 4096) { while (i < in.size() && isspace(in[i])) i++; if (i < in.size() && in[i] == '#') { while (i < in.size() && in[i] != '\n') i++; continue; }
        long v = 0; size_t st = i; while (i < in.size() && isdigit(in[i])) v = v * 10 + (in[i++] - '0'); if (i == st) return 0; vals[k++] = (int)v; }
    if (k < 3 || i >= in.size() || !isspace(in[i]) || vals[2] != 255) return 0; i++;
    size_t px = (size_t)vals[0] * vals[1] * (in[1] == '6' ? 3 : 1); if (i + px != in.size()) return 0;
    kind = in[1] - '0'; return i;
}
static bool y4m_split(const B &in, size_t &hl, size_t &fb, string &pf, int &w, int &h) {   // plain Y4M: header + ("FRAME\n" + frame)*
    hl = fb = 0; if (in.size() < 32 || memcmp(in.data(), "YUV4MPEG2 ", 10)) return false;
    size_t e = 0; while (e < in.size() && in[e] != '\n') e++; if (e >= in.size()) return false; hl = e + 1;
    string hd((const char *)in.data(), e); w = h = 0; string cs = "420jpeg";
    size_t p = 0; while ((p = hd.find(' ', p)) != string::npos) { p++; if (p < hd.size()) { char t = hd[p]; string v = hd.substr(p + 1, hd.find(' ', p) - p - 1);
        if (t == 'W') w = atoi(v.c_str()); else if (t == 'H') h = atoi(v.c_str()); else if (t == 'C') cs = v; } }
    if (w <= 0 || h <= 0) return false;
    if (cs.rfind("420", 0) == 0) { pf = "yuv420p"; fb = (size_t)w * h * 3 / 2; } else if (cs == "444") { pf = "yuv444p"; fb = (size_t)w * h * 3; }
    else if (cs == "422") { pf = "yuv422p"; fb = (size_t)w * h * 2; } else if (cs == "mono") { pf = "gray"; fb = (size_t)w * h; } else return false;
    if (w % 2 || h % 2) return false;
    size_t pos = hl, nfr = 0; while (pos < in.size()) { if (pos + 6 + fb > in.size() || memcmp(in.data() + pos, "FRAME\n", 6)) return false; pos += 6 + fb; nfr++; }
    return nfr > 0;
}
// HX 13: binary PGM/PPM images embedded inside a file (for example the pixels H-Deflate unpacks from a PNG), (offset, length) pairs
static vector<pair<size_t, size_t>> find_embedded_pnm(const B &in) {
    vector<pair<size_t, size_t>> r; size_t n = in.size();
    for (size_t i = 0; i + 16 < n; i++) {
        if (in[i] != 'P' || (in[i + 1] != '5' && in[i + 1] != '6') || in[i + 2] != '\n') continue;
        size_t j = i + 3; long v[3]; int k = 0;
        while (k < 3 && j < n && j < i + 64) { if (!isdigit(in[j])) { j++; continue; } long x = 0; while (j < n && isdigit(in[j])) x = x * 10 + (in[j++] - '0'); v[k++] = x; }
        if (k < 3 || v[2] != 255 || v[0] <= 0 || v[1] <= 0 || j >= n || in[j] != '\n') continue;
        j++; size_t px = (size_t)v[0] * v[1] * (in[i + 1] == '6' ? 3 : 1);
        if (px < 16384 || j + px > n) continue;
        r.push_back({i, j + px - i}); i = j + px - 1;
    }
    return r;
}
// HX 13: JPEG files embedded inside a file (PDF DCTDecode streams, Office media): (offset, length) pairs, SOI to EOI
static vector<pair<size_t, size_t>> find_embedded_jpeg(const B &in) {
    vector<pair<size_t, size_t>> r; size_t n = in.size();
    for (size_t i = 0; i + 4 < n; i++) {
        if (in[i] != 0xFF || in[i + 1] != 0xD8 || in[i + 2] != 0xFF) continue;
        size_t p = i + 2; bool ok = false;
        while (p + 4 <= n && in[p] == 0xFF) {   // marker segments up to SOS
            uint8_t m = in[p + 1]; if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
            size_t L = ((size_t)in[p + 2] << 8) | in[p + 3]; if (L < 2 || p + 2 + L > n) break;
            if (m == 0xDA) { p += 2 + L; ok = true; break; }
            p += 2 + L;
        }
        if (!ok) continue;
        size_t e = p; while (e + 1 < n && !(in[e] == 0xFF && in[e + 1] == 0xD9)) e++;
        if (e + 1 >= n) continue;
        size_t len = e + 2 - i; if (len < 8192) { i = e + 1; continue; }
        r.push_back({i, len}); i = e + 1;
    }
    return r;
}
#include "hx_ppmd.h"
#include "hx_lzma.h"
#include "hx_bcj2.h"
#include <brotli/encode.h>
#include <brotli/decode.h>
// HX 14 ROUTES (4 Nov 2026, Michael: "make the HX complete more thorough for the weak spots"). All class A (public domain / MIT / BSD):
//   mode 12  LZMA (Igor Pavlov's public-domain LZMA SDK) with the lzma-hi choice of lc/lp/pb and record delta, screened on samples
//   mode 13  BCJ2 x86 branch split (public domain) + each stream to the better of LZMA / BSC; the 7-Zip executable recipe
//   mode 14  PPMd var.I / var.H (Dmitry Shkarin, public domain; Igor Pavlov's C code), order chosen on samples
//   mode 15  Brotli quality 11, large window (Google, MIT)
//   media route 7: WavPack -hhx6 (David Bryant, BSD) for WAV files
static B sample_of(const B &in, size_t want) {   // up to 4 evenly spread pieces (his sample-screening idea)
    if (in.size() <= want) return in; B s; size_t piece = want / 4;
    for (int k = 0; k < 4; k++) { size_t off = (in.size() - piece) * k / 3; s.insert(s.end(), in.begin() + off, in.begin() + off + piece); }
    return s;
}
static B lzma_route(const B &in, int rec, int T, bool dbg) {
    vector<hxlzma::Cfg> cand = hxlzma::candidates(rec); B smp = sample_of(in, (size_t)4 << 20);
    vector<pair<size_t, int>> sc; for (size_t i = 0; i < cand.size(); i++) { B b = hxlzma::enc(smp.data(), smp.size(), cand[i], T); if (!b.empty()) sc.push_back({b.size(), (int)i}); }
    sort(sc.begin(), sc.end()); B best;
    for (size_t k = 0; k < sc.size() && k < (in.size() == smp.size() ? 1u : 2u); k++) { B b = hxlzma::enc(in.data(), in.size(), cand[sc[k].second], T); if (!b.empty() && (best.empty() || b.size() < best.size())) best.swap(b); }
    if (dbg && !best.empty()) fprintf(stderr, "  lzma: %zu\n", best.size());
    return best;
}
static B bsc_enc(const uint8_t *p, size_t n);
static void bsc_dec(const uint8_t *p, uint8_t *out, size_t n, int T);
static B bcj2_route(const B &in, int T) {   // body: 4 x [len 4][coder 1][orig len 4] then the payloads
    vector<uint8_t> s[4]; if (!hxbcj2::enc(in.data(), in.size(), s)) return B();
    B body; vector<B> pays(4); vector<uint8_t> coder(4);
    for (int i = 0; i < 4; i++) {
        if (i == 3 || s[i].empty()) { pays[i] = s[i]; coder[i] = 0; continue; }
        hxlzma::Cfg c; if (i > 0) { c.lc = 0; c.lp = 2; c.pb = 2; }
        B l = hxlzma::enc(s[i].data(), s[i].size(), c, T); coder[i] = 1; pays[i] = l;
        if (i == 0) { B b = bsc_enc(s[i].data(), s[i].size()); if (!b.empty() && b.size() < pays[i].size()) { pays[i] = b; coder[i] = 2; } }
        if (pays[i].empty() || pays[i].size() >= s[i].size()) { pays[i] = s[i]; coder[i] = 0; }
    }
    for (int i = 0; i < 4; i++) { put32(body, (uint32_t)pays[i].size()); body.push_back(coder[i]); put32(body, (uint32_t)s[i].size()); }
    for (int i = 0; i < 4; i++) body.insert(body.end(), pays[i].begin(), pays[i].end());
    return body;
}
static bool bcj2_unroute(const uint8_t *p, size_t m, uint8_t *out, size_t n, int T) {
    if (m < 36) return false; size_t pl[4], ol[4]; int cd[4]; const uint8_t *q2 = p;
    for (int i = 0; i < 4; i++) { pl[i] = get32(q2); cd[i] = q2[4]; ol[i] = get32(q2 + 5); q2 += 9; }
    vector<uint8_t> s[4];
    for (int i = 0; i < 4; i++) { s[i].resize(ol[i]);
        if (cd[i] == 0) { if (pl[i] != ol[i]) return false; memcpy(s[i].data(), q2, ol[i]); }
        else if (cd[i] == 1) { if (!hxlzma::dec(q2, pl[i], s[i].data(), ol[i])) return false; }
        else bsc_dec(q2, s[i].data(), ol[i], T);
        q2 += pl[i]; }
    return hxbcj2::dec(s, out, n);
}
static B ppmd_route(const B &in, bool dbg) {
    struct C { int v, o; }; vector<C> cand = {{8, 16}, {8, 12}, {8, 8}, {8, 6}, {8, 4}, {7, 32}, {7, 64}};
    unsigned mem = in.size() < ((size_t)64 << 20) ? 256 : 1024;
    B smp = sample_of(in, (size_t)4 << 20); vector<pair<size_t, int>> sc;
    for (size_t i = 0; i < cand.size(); i++) { B b = hxppmd::enc(smp.data(), smp.size(), cand[i].v, cand[i].o, mem); if (!b.empty()) sc.push_back({b.size(), (int)i}); }
    sort(sc.begin(), sc.end()); B best;
    for (size_t k = 0; k < sc.size() && k < (in.size() == smp.size() ? 1u : 2u); k++) { B b = hxppmd::enc(in.data(), in.size(), cand[sc[k].second].v, cand[sc[k].second].o, mem); if (!b.empty() && (best.empty() || b.size() < best.size())) best.swap(b); }
    if (dbg && !best.empty()) fprintf(stderr, "  ppmd: %zu\n", best.size());
    return best;
}
// Brotli-hi (Michael Harrington's encoder-only Brotli 11 settings search, 26 Oct 2026): block size, context mode and distance
// postfix chosen per file on a sample; standard Brotli stream (large window), read by the stock decoder. Config 0 is plain Brotli 11.
struct BHi { int lgblock, mode, npost, nolcm; };
static const BHi BHI[] = {{0, 0, -1, 0}, {0, 1, 1, 0}, {16, 1, 2, 0}, {18, 1, 1, 0}, {20, 1, 2, 0}, {0, 0, 2, 0}, {18, 0, -1, 1}};
static B brotli_cfg(const uint8_t *d, size_t n, int k) {
    size_t out_n = BrotliEncoderMaxCompressedSize(n); if (!out_n) out_n = n + 1024; B o(out_n + 1);
    BrotliEncoderState *e = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr); if (!e) return B();
    int lg = 10; while (lg < 30 && ((size_t)1 << lg) < n) lg++;
    BrotliEncoderSetParameter(e, BROTLI_PARAM_QUALITY, 11); BrotliEncoderSetParameter(e, BROTLI_PARAM_LARGE_WINDOW, 1); BrotliEncoderSetParameter(e, BROTLI_PARAM_LGWIN, (uint32_t)max(lg, 16));
    BrotliEncoderSetParameter(e, BROTLI_PARAM_SIZE_HINT, (uint32_t)min(n, (size_t)1 << 30));
    const BHi &b = BHI[k]; if (b.lgblock) BrotliEncoderSetParameter(e, BROTLI_PARAM_LGBLOCK, (uint32_t)b.lgblock);
    if (b.mode) BrotliEncoderSetParameter(e, BROTLI_PARAM_MODE, (uint32_t)b.mode); if (b.npost >= 0) BrotliEncoderSetParameter(e, BROTLI_PARAM_NPOSTFIX, (uint32_t)b.npost);
    if (b.nolcm) BrotliEncoderSetParameter(e, BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING, 1);
    size_t ai = n, ao = o.size(); const uint8_t *ni = d; uint8_t *no = o.data();
    bool ok = BrotliEncoderCompressStream(e, BROTLI_OPERATION_FINISH, &ai, &ni, &ao, &no, nullptr) && BrotliEncoderIsFinished(e);
    BrotliEncoderDestroyInstance(e); if (!ok) return B(); o.resize(o.size() - ao); return o;
}
static B brotli_route(const B &in) {
    B o = brotli_cfg(in.data(), in.size(), 0); if (o.empty() || getenv("HX_NO_HI")) return o;
    B s = sample_of(in, (size_t)1 << 20); size_t bs = SIZE_MAX; int best = 0;
    for (int k = 0; k < (int)(sizeof BHI / sizeof BHI[0]); k++) { B t = brotli_cfg(s.data(), s.size(), k); if (!t.empty() && t.size() < bs) { bs = t.size(); best = k; } }
    if (best > 0) { B h = brotli_cfg(in.data(), in.size(), best); if (!h.empty() && h.size() < o.size()) o.swap(h); }   // never worse than plain Brotli 11
    return o;
}
static bool brotli_unroute(const uint8_t *p, size_t m, uint8_t *out, size_t n) {
    BrotliDecoderState *d = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr); if (!d) return false;
    BrotliDecoderSetParameter(d, BROTLI_DECODER_PARAM_LARGE_WINDOW, 1);
    size_t ai = m, ao = n; const uint8_t *ni = p; uint8_t *no = out;
    BrotliDecoderResult r = BrotliDecoderDecompressStream(d, &ai, &ni, &ao, &no, nullptr); BrotliDecoderDestroyInstance(d);
    return r == BROTLI_DECODER_RESULT_SUCCESS && ao == 0;
}
//   mode 16  HCM v3.6.1 max (Michael's own context-mixing compressor, MIT), Sets mode for big files; external program HX_HCM
static B hcm_run(const char *op, const uint8_t *d, size_t n) {
    const char *h = getenv("HX_HCM"); if (!h) return B();
    string dir = mz_tmp(), a = dir + "/a", b = dir + "/b"; writef(a.c_str(), d, n);
    string cmd = string("\"") + h + "\" " + op + " \"" + a + "\" \"" + b + "\" > " HX_NULL " 2>&1";
    bool ok = hx_system(cmd.c_str()) == 0; B o; if (ok) { FILE *f = fopen(b.c_str(), "rb"); if (f) { fclose(f); o = readf(b.c_str()); } }
    remove(a.c_str()); remove(b.c_str()); rmdir(dir.c_str()); return o;
}
static B hcm_route(const B &in, size_t best_now, bool dbg) {
    if (!getenv("HX_HCM")) return B();
    B smp = sample_of(in, (size_t)2 << 20); B hs = hcm_run("x", smp.data(), smp.size());
    if (hs.empty()) return B();
    double pred = (double)hs.size() / smp.size() * in.size();
    if (dbg) fprintf(stderr, "  hcm screen: sample predicts %.0f vs best %zu\n", pred, best_now);
    if (in.size() > smp.size() && pred > 1.02 * best_now) return B();   // screened out
    B o = hcm_run(in.size() > ((size_t)24 << 20) ? "t" : "x", in.data(), in.size());
    if (o.empty()) return o;
    B chk = hcm_run("d", o.data(), o.size()); if (chk != in) return B();   // keep only a verified round trip
    return o;
}
static B media_dec(int route, const uint8_t *p, size_t m, size_t n);
static B media_enc(int route, const B &in) {   // returns the mode-10 body (route, kept header, payload), or empty if not possible
    string d = mz_tmp(), a = d + "/a", b = d + "/b", c = d + "/c"; B body; body.push_back((uint8_t)route); size_t keep = 0; bool ok = false;
    auto cleanup = [&]() { remove(a.c_str()); remove(b.c_str()); remove(c.c_str()); remove((d + "/c.ppm").c_str()); remove((d + "/c.pgm").c_str()); remove((d + "/a.jpg").c_str()); remove((d + "/a.pjg").c_str()); rmdir(d.c_str()); };
    if (route == 1 || route == 2) { int kind; keep = pnm_header(in, kind); if (!keep || (route == 2 && kind != 6)) { cleanup(); return B(); }
        // a canonical PNM of the same pixels is what the specialist sees; the original header is kept as it is
        string ext = kind == 6 ? ".ppm" : ".pgm"; string hdr = string("P") + char('0' + kind) + "\n" + to_string(0) + "\n";
        int vals[2]; { size_t i = 2; int k = 0; while (k < 2) { while (isspace(in[i])) i++; if (in[i] == '#') { while (in[i] != '\n') i++; continue; } long v = 0; while (isdigit(in[i])) v = v * 10 + (in[i++] - '0'); vals[k++] = (int)v; } }
        hdr = string("P") + char('0' + kind) + "\n" + to_string(vals[0]) + " " + to_string(vals[1]) + "\n255\n";
        B can(hdr.begin(), hdr.end()); can.insert(can.end(), in.begin() + keep, in.end()); writef((a + ext).c_str(), can.data(), can.size());
        if (route == 1) ok = sh(q(tool("HX_CJXL")) + " " + q(a + ext) + " " + q(b) + " -d 0 -e 9 --quiet");
        else ok = sh(q(tool("HX_CWEBP")) + " -quiet -lossless -z 9 " + q(a + ext) + " -o " + q(b));
        remove((a + ext).c_str());
    } else if (route == 3 || route == 4) { if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xD8 || in[2] != 0xFF) { cleanup(); return B(); }
        writef((d + "/a.jpg").c_str(), in.data(), in.size());
        if (route == 3) { ok = hx_system((HX_CD + q(d) + " && " + q(tool("HX_PACKJPG")) + " a -np -o a.jpg > " HX_NULL " 2>&1").c_str()) == 0; if (ok) rename((d + "/a.pjg").c_str(), b.c_str()); }
        else ok = sh(q(tool("HX_CJXL")) + " " + q(d + "/a.jpg") + " " + q(b) + " --lossless_jpeg=1 --quiet -e 9");
    } else if (route == 7) { if (in.size() < 44 || memcmp(in.data(), "RIFF", 4) || memcmp(in.data() + 8, "WAVE", 4)) { cleanup(); return B(); }
        writef((a + ".wav").c_str(), in.data(), in.size());
        ok = sh(q(tool("HX_WAVPACK")) + " -q -y -hhx6 " + q(a + ".wav") + " -o " + q(b + ".wv"));
        if (ok) rename((b + ".wv").c_str(), b.c_str());
        remove((a + ".wav").c_str());
    } else if (route == 5 || route == 6) { size_t hl, fb; string pf; int w, h; if (!y4m_split(in, hl, fb, pf, w, h)) { cleanup(); return B(); } keep = hl;
        writef(a.c_str(), in.data(), in.size());
        string codec = route == 5 ? "-c:v ffv1 -level 3 -coder 1 -context 1 -g 1" : "-c:v libx264 -qp 0 -preset veryslow";
        ok = sh(q(tool("HX_FFMPEG")) + " -v error -y -f yuv4mpegpipe -i " + q(a) + " " + codec + " -f matroska " + q(b));
    }
    if (!ok) { cleanup(); return B(); }
    B pay = readf(b.c_str()); cleanup();
    put32(body, (uint32_t)keep); body.insert(body.end(), in.begin(), in.begin() + keep); body.insert(body.end(), pay.begin(), pay.end());
    B chk = media_dec(route, body.data() + 1, body.size() - 1, in.size());   // keep it only if it decodes back byte for byte
    if (chk != in) return B();
    return body;
}
static B media_dec(int route, const uint8_t *p, size_t m, size_t n) {
    size_t keep = get32(p); const uint8_t *hd = p + 4; const uint8_t *pay = hd + keep; size_t pl = m - 4 - keep;
    string d = mz_tmp(), a = d + "/a", b = d + "/b"; B out; writef(a.c_str(), pay, pl); bool ok = false;
    auto cleanup = [&]() { remove(a.c_str()); remove(b.c_str()); remove((d + "/b.ppm").c_str()); remove((d + "/b.pgm").c_str()); remove((d + "/a.pjg").c_str()); remove((d + "/a.jpg").c_str()); rmdir(d.c_str()); };
    if (route == 1 || route == 2) { int kind = hd[1] - '0'; string ext = kind == 6 ? ".ppm" : ".pgm";
        if (route == 1) ok = sh(q(tool("HX_DJXL")) + " " + q(a) + " " + q(b + ext) + " --quiet"); else ok = sh(q(tool("HX_DWEBP")) + " -quiet " + q(a) + " -ppm -o " + q(b + ext));
        if (ok) { B r = readf((b + ext).c_str()); int k2; size_t h2 = pnm_header(r, k2); size_t px = n - keep;
            if (h2 && r.size() - h2 == px) { out.assign(hd, hd + keep); out.insert(out.end(), r.begin() + h2, r.end()); } } }
    else if (route == 3 || route == 4) {
        if (route == 3) { rename(a.c_str(), (d + "/a.pjg").c_str()); ok = hx_system((HX_CD + q(d) + " && " + q(tool("HX_PACKJPG")) + " x -np -o a.pjg > " HX_NULL " 2>&1").c_str()) == 0; if (ok) out = readf((d + "/a.jpg").c_str()); }
        else { ok = sh(q(tool("HX_DJXL")) + " " + q(a) + " " + q(b) + ".jpg --quiet"); if (ok) { out = readf((b + ".jpg").c_str()); remove((b + ".jpg").c_str()); } } }
    else if (route == 7) { rename(a.c_str(), (a + ".wv").c_str()); ok = sh(q(tool("HX_WVUNPACK")) + " -q -y " + q(a + ".wv") + " -o " + q(b + ".wav"));
        if (ok) { out = readf((b + ".wav").c_str()); remove((b + ".wav").c_str()); } remove((a + ".wv").c_str()); }
    else if (route == 5 || route == 6) { B hdr(hd, hd + keep); size_t hl, fb; string pf; int w, h;
        B probe = hdr; probe.insert(probe.end(), {'F', 'R', 'A', 'M', 'E', '\n'}); size_t est = 0;
        { string hs((const char *)hd, keep); w = h = 0; string cs = "420jpeg"; size_t p2 = 0; while ((p2 = hs.find(' ', p2)) != string::npos) { p2++; if (p2 < hs.size()) { char t = hs[p2]; string v = hs.substr(p2 + 1, hs.find_first_of(" \n", p2) - p2 - 1);
            if (t == 'W') w = atoi(v.c_str()); else if (t == 'H') h = atoi(v.c_str()); else if (t == 'C') cs = v; } }
          if (cs.rfind("420", 0) == 0) { pf = "yuv420p"; fb = (size_t)w * h * 3 / 2; } else if (cs == "444") { pf = "yuv444p"; fb = (size_t)w * h * 3; } else if (cs == "422") { pf = "yuv422p"; fb = (size_t)w * h * 2; } else { pf = "gray"; fb = (size_t)w * h; } }
        ok = sh(q(tool("HX_FFMPEG")) + " -v error -y -i " + q(a) + " -f rawvideo -pix_fmt " + pf + " " + q(b));
        if (ok) { B raw = readf(b.c_str()); if (fb && raw.size() % fb == 0) { out = hdr; for (size_t f = 0; f < raw.size(); f += fb) { out.insert(out.end(), {'F', 'R', 'A', 'M', 'E', '\n'}); out.insert(out.end(), raw.begin() + f, raw.begin() + f + fb); } } }
        (void)est; (void)hl; }
    cleanup(); if (out.size() != n) out.clear(); return out;
}

#include "hx_router.h"
// ===== HX 1.1.0 (10 Nov 2026): block mode, memory limit, thread flag, stdin/stdout =====
// Asked for on encode.su (xman, 10 Nov 2026): a 1.6 GB file did not fit in 16 GB of memory, and HX could not read a pipe.
// Files made without --block / --memlimit / a pipe are the same HXF1 format as HX 1.0, byte for byte.
// Block files (HXB1): "HXB1", version 1, block size (8 bytes), then for each block its length (8 bytes) and an HXF1 record
// of that block, then a 0 length and the total original size (8 bytes). HX 1.1 decodes HX 1.0 files and HXB1 files.
static FILE *g_msg = stdout;     // status lines; stderr when the output goes to stdout
static string g_prefix;          // "block k: " in block mode
static B compress_one(const B &in, int T, bool complete, const string &self, const string &name) {
    double t0 = now(); g_zhi = -1;   // HX 1.1: settings chosen per file are chosen again for every block
        size_t n = in.size(); B best; int mode = 0;
        if (n == 0) { B o = {'H', 'X', 'F', '1', 0, 0, 0, 0, 0, 0, 0, 0, 0}; fprintf(g_msg, "%shxf: empty\n", g_prefix.c_str()); return o; }
        // candidate 1: Sets (64 KB or 65532-byte windows) or none, each followed by the Exchange lines
        g_w16 = detect_width(in.data(), n, 2); g_w8 = detect_width(in.data(), n, 1); g_rec = detect_record(in.data(), n);   // image lines: row width; record lanes: record length
        bool dbg = getenv("HX_DEBUG") != nullptr; auto lap = [&](const char *w) { if (dbg) fprintf(stderr, "  %-28s %7.2f s  best %zu\n", w, now() - t0, best.size()); };
        const vector<int> *lines_allow = abl_lines_off ? &abl_plain : nullptr;
        if (abl_lines_off || abl_sets_off || abl_repeat_off || abl_split_off || !abl_gmap.empty() || g_keepmax != 6 || !g_probe)
            fprintf(g_msg, "hx ablation: sampler=%s lines=%s sets=%s repeat=%s split=%s\n", !g_probe ? "every line in full" : g_keepmax == 1 ? "sample picks the winner" : "Harrington",
                abl_lines_off ? "plain entropy coding" : "Harrington", abl_sets_off ? "plain blocks" : !abl_gmap.empty() ? "outside map" : "Harrington",
                abl_repeat_off ? "off" : "Harrington", abl_split_off ? "whole-file best-of" : "Harrington");
        Plan none; plan_segments(none, n); vector<int> won, keptall; vector<B> nparts; best = hx_encode(in, none, T, lines_allow, &won, &nparts, &keptall); mode = 1;
        // the Sets passes: HX_ALLOW=all (default: each Sets segment gets its own probe), won (only the winners without Sets), kept (lines kept by the probes)
        const char *ae = getenv("HX_ALLOW"); string am = ae ? ae : "all";
        vector<int> allow = am == "won" ? won : keptall; sort(allow.begin(), allow.end()); allow.erase(unique(allow.begin(), allow.end()), allow.end());
        lap("lines (no Sets)");
        vector<int> gmap;
        if (!abl_gmap.empty()) { B g = readf(abl_gmap.c_str()); size_t nw = (n + 65535) / 65536;
            if (g.size() < 13 || memcmp(g.data(), "GMP1", 4) || get32(g.data() + 4) != 65536 || get32(g.data() + 8) != nw || g[12] > 4 || g.size() < 13 + nw) { fprintf(stderr, "hx: grouping map does not match this file\n"); exit(1); }
            for (size_t w = 0; w < nw; w++) { if (g[13 + w] > 3) { fprintf(stderr, "hx: grouping map class out of range\n"); exit(1); } gmap.push_back(g[13 + w]); } }
        if (!abl_sets_off) for (size_t sw : {(size_t)65536, (size_t)65532}) { if (!gmap.empty() && sw != 65536) continue;
            SW = sw; Plan P; P.sets = true; P.sw = sw; P.lab = gmap.empty() ? sets_labels(in.data(), n) : gmap;
            if (*max_element(P.lab.begin(), P.lab.end()) == 0) continue; plan_segments(P, n);
            B c = hx_encode(in, P, T, abl_lines_off ? &abl_plain : (g_probe && am != "all") ? &allow : nullptr); if (c.size() < best.size()) best.swap(c); }
        // per-segment zstd -19 (the split mode needs it) and, for the estimate below, per-segment zstd -1
        Plan none2; plan_segments(none2, n); size_t ns = none2.soff.size(); vector<double> z19(ns, 0), z1(ns, 0); vector<char> mark(ns, 0);
        runpar(ns, T, [&](size_t k) { B zz(ZSTD_compressBound(none2.slen[k])); const uint8_t *sp = in.data() + none2.soff[k];
            size_t r = ZSTD_compress(zz.data(), zz.size(), sp, none2.slen[k], 19); z19[k] = ZSTD_isError(r) ? (double)none2.slen[k] : (double)r;
            if (!ZSTD_isError(r) && nparts[k].size() < 0.97 * r) mark[k] = 1;
            if (g_probe) { size_t r1 = ZSTD_compress(zz.data(), zz.size(), sp, none2.slen[k], 1); z1[k] = ZSTD_isError(r1) ? (double)none2.slen[k] : (double)r1; } });
        lap("zstd -19 per segment");
        // HX Fast 8: the whole-file repeat line (zstd -19 long) is only made when its estimate - per-segment zstd -19 times the
        // gain long-distance matching gives at level 1 - comes within 5% of the best so far (HX_ALWAYS_LONG=1 or --full: always)
        double gain = 1.0; bool est_on = g_probe && !getenv("HX_ALWAYS_LONG");
        if (est_on) { double s1 = 0; for (double v : z1) s1 += v; B l1 = zstd_long(in, T, 1); gain = min(1.0, l1.size() / max(1.0, s1)); }
        auto est = [&](bool unmarked_only) { double e = 0; for (size_t k = 0; k < ns; k++) if (!unmarked_only || !mark[k]) e += z19[k]; return e * gain; };
        // candidate 2: the cascade's repeat line (whole file, long-distance matching)
        if (!abl_repeat_off && (!est_on || est(false) < 1.05 * best.size())) { B z = zstd_long(in, T, 19); if (z.size() < best.size()) { best.swap(z); mode = 2; } }
        if (dbg) fprintf(stderr, "  estimate: repeat line %.0f (gain %.3f)\n", est(false), gain);
        lap("repeat line (zstd -19 long)");
        // candidate 3 (split, the cascade with HX lines): segments where the Exchange lines clearly beat zstd -19 on
        // that segment go to HX; everything else stays together in ONE repeat-line stream (so long matches survive)
        { size_t nm = 0, hxm = 0; for (size_t k = 0; k < ns; k++) if (mark[k]) { nm++; hxm += nparts[k].size(); }
          if (!abl_split_off && nm > 0 && nm < ns && (!est_on || est(true) + hxm < 1.03 * best.size())) {
              B rest; for (size_t k = 0; k < ns; k++) if (!mark[k]) rest.insert(rest.end(), in.data() + none2.soff[k], in.data() + none2.soff[k] + none2.slen[k]);
              B zr = zstd_long(rest, T, 19); B c; put32(c, (uint32_t)ns); for (size_t k = 0; k < ns; k++) c.push_back(mark[k]);
              put32(c, (uint32_t)zr.size()); c.insert(c.end(), zr.begin(), zr.end());
              for (size_t k = 0; k < ns; k++) if (mark[k]) { put32(c, (uint32_t)nparts[k].size()); c.insert(c.end(), nparts[k].begin(), nparts[k].end()); }
              if (c.size() < best.size()) { best.swap(c); mode = 3; } } }
        lap("split");
        // ===== HX Complete 15: the expensive routes as a table, picked by the learned router =====
        if (complete) {
        size_t base_best = best.size(); int base_mode = mode; bool classA = getenv("HX_CLASS_A") != nullptr;
        size_t x86cv = 0; { B t = in; x86cv = x86_filter(t.data(), n, true); }
        int kind; bool pnm = pnm_header(in, kind) > 0, jpg = n > 4 && in[0] == 0xFF && in[1] == 0xD8 && in[2] == 0xFF; size_t hl, fb; string pf; int w, h; bool y4m = y4m_split(in, hl, fb, pf, w, h);
        bool wav = n > 44 && !memcmp(in.data(), "RIFF", 4) && !memcmp(in.data() + 8, "WAVE", 4) && !tool("HX_WAVPACK").empty();
        bool media = getenv("HX_MEDIA") != nullptr; int fmt = pnm ? 1 : jpg ? 2 : wav ? 3 : y4m ? 4 : 0;
        double linesz = 0, z19s = 0, z1s = 0; for (auto &pp : nparts) linesz += pp.size(); for (double v : z19) z19s += v; for (double v : z1) z1s += v;
        vector<double> F = hx15_features(in, base_best, linesz, z19s, z1s, x86cv, fmt);
        string rlog; size_t hcm_force = getenv("HX_ALLROUTES") ? ((size_t)32 << 20) : 0;
        auto take = [&](B &c, int md) { if (!c.empty() && c.size() < best.size()) { best.swap(c); mode = md; } };
        auto applicable = [&](int id) -> bool { switch (id) {
            case 5: return !classA; case 6: return !abl_split_off; case 13: case 64: return x86cv * 1000 >= n; case 16: return getenv("HX_HCM") != nullptr;
            case 17: return getenv("HX_HCM37") != nullptr; case 10: return media && (pnm || jpg || wav || (y4m && !classA));
            case 11: return media && !pnm && !jpg && !y4m && !getenv("HX_NOEMBED"); case 18: return g_w16 >= 16 && getenv("HX_CJXL") != nullptr && !pnm;
            default: return true; } };
        auto run_route = [&](int id) { double ts = now(); size_t sz = 0;
            switch (id) {
            case 4: { B b4 = bsc_enc(in.data(), n); sz = b4.size(); take(b4, 4); break; }
            case 5: { B b5 = bz3_enc(in.data(), n); sz = b5.size(); take(b5, 5); break; }
            case 6: { vector<char> mk(ns, 0); runpar(ns, T, [&](size_t k) { B t = bsc_enc(in.data() + none2.soff[k], none2.slen[k]); if (nparts[k].size() < 0.97 * t.size()) mk[k] = 1; });
                size_t nm = 0; for (char c : mk) nm += c;
                if (nm > 0 && nm < ns) { B rest; for (size_t k = 0; k < ns; k++) if (!mk[k]) rest.insert(rest.end(), in.data() + none2.soff[k], in.data() + none2.soff[k] + none2.slen[k]);
                    for (int md : {6, 7}) { if (md == 7 && classA) continue; B zr = md == 6 ? bsc_enc(rest.data(), rest.size()) : bz3_enc(rest.data(), rest.size());
                        B c; put32(c, (uint32_t)ns); for (size_t k = 0; k < ns; k++) c.push_back(mk[k]); put32(c, (uint32_t)zr.size()); c.insert(c.end(), zr.begin(), zr.end());
                        for (size_t k = 0; k < ns; k++) if (mk[k]) { put32(c, (uint32_t)nparts[k].size()); c.insert(c.end(), nparts[k].begin(), nparts[k].end()); }
                        if (!sz || c.size() < sz) sz = c.size(); take(c, md); } }
                break; }
            case 12: { int rec = g_rec > 1 ? g_rec : 0; B b = lzma_route(in, rec, T, dbg); sz = b.size(); take(b, 12); break; }
            case 14: { B b = ppmd_route(in, dbg); sz = b.size(); take(b, 14); break; }
            case 15: { B b = brotli_route(in); sz = b.size(); take(b, 15); break; }
            case 13: { B b = bcj2_route(in, T); sz = b.size(); take(b, 13); break; }
            case 16: { B b = hcm_route(in, n <= hcm_force ? SIZE_MAX / 4 : best.size(), dbg); sz = b.size(); take(b, 16); break; }
            case 17: { B b = hcm37_route(in, x86cv * 1000 >= n && getenv("HX_KZF")); sz = b.size(); take(b, 17); break; }
            case 64: { B xf = in; x86_filter(xf.data(), n, true); B b4 = bsc_enc(xf.data(), n); sz = b4.size(); take(b4, 64 + 4);
                if (!classA) { B b5 = bz3_enc(xf.data(), n); if (b5.size() < sz) sz = b5.size(); take(b5, 64 + 5); } break; }
            case 10: { vector<int> routes; if (pnm) routes = {1, 2}; if (jpg) routes = classA ? vector<int>{4} : vector<int>{3, 4}; if (wav) routes = {7}; if (y4m && !classA) { routes = {5}; if (getenv("HX_MZIP") || getenv("HX_RESEARCH")) routes.push_back(6); }
                for (int r : routes) { B body = media_enc(r, in); if (!body.empty() && (!sz || body.size() < sz)) sz = body.size(); take(body, 10); } break; }
            case 18: { B b = img16_route(in, g_w16, dbg); sz = b.size(); take(b, 18); break; }
            case 11: { size_t before = best.size(); int bm0 = mode;
            {
                vector<pair<size_t, size_t>> imgs = find_embedded_pnm(in), jp = find_embedded_jpeg(in);
                vector<pair<pair<size_t, size_t>, int>> all; for (auto &im : imgs) all.push_back({im, 1}); for (auto &im : jp) all.push_back({im, 4});
                sort(all.begin(), all.end()); { vector<pair<pair<size_t, size_t>, int>> nz; size_t end = 0; for (auto &e : all) if (e.first.first >= end) { nz.push_back(e); end = e.first.first + e.first.second; } all.swap(nz); }
                if (!all.empty()) {
                    B c; vector<pair<size_t, size_t>> kept; vector<B> bodies; size_t pixtot = 0, bodytot = 0;
                    for (auto &e : all) { auto &im = e.first; B sub(in.begin() + im.first, in.begin() + im.first + im.second); B body = media_enc(e.second, sub);
                        if (!body.empty() && body.size() < (e.second == 1 ? 0.9 : 0.97) * sub.size()) { kept.push_back(im); bodies.push_back(body); pixtot += sub.size(); bodytot += body.size(); } }
                    if (!kept.empty()) {
                        B rest; size_t pos = 0; for (auto &im : kept) { rest.insert(rest.end(), in.begin() + pos, in.begin() + im.first); pos = im.first + im.second; } rest.insert(rest.end(), in.begin() + pos, in.end());
                        string d = mz_tmp(), a = d + "/r", b = d + "/r.h"; writef(a.c_str(), rest.data(), rest.size());
                        bool ok = rest.empty() || sh(string(HX_NOEMBED_PREFIX) + q(self) + " c " + q(a) + " " + q(b) + " " + to_string(T) + " --complete > " HX_NULL "");
                        B rb = (ok && !rest.empty()) ? readf(b.c_str()) : B(); remove(a.c_str()); remove(b.c_str()); rmdir(d.c_str());
                        if (ok) { put32(c, (uint32_t)kept.size()); for (size_t k = 0; k < kept.size(); k++) { put32(c, (uint32_t)kept[k].first); put32(c, (uint32_t)kept[k].second); put32(c, (uint32_t)bodies[k].size()); c.insert(c.end(), bodies[k].begin(), bodies[k].end()); }
                            put32(c, (uint32_t)rb.size()); c.insert(c.end(), rb.begin(), rb.end());
                            if (c.size() < best.size()) { best.swap(c); mode = 11; } }
                        if (dbg) fprintf(stderr, "  embedded images: %zu kept, %zu -> %zu pixel bytes\n", kept.size(), pixtot, bodytot);
                    }
                    } }
                sz = (mode == 11 && bm0 != 11) ? best.size() : (best.size() < before ? best.size() : 0); break; }
            }
            char e[96]; snprintf(e, sizeof e, "%s%d:%zu:%.3f", rlog.empty() ? "" : "|", id, sz, now() - ts); rlog += e;
            if (dbg) fprintf(stderr, "  route %-3d %10zu bytes  %7.2f s\n", id, sz, now() - ts); };
        vector<int> ALL = {4, 5, 6, 12, 14, 15, 13, 16, 17, 64, 10, 11, 18}, app; for (int id : ALL) if (applicable(id)) app.push_back(id);
        if (const char *only = getenv("HX_ONLY")) { vector<int> keep; for (int id : app) { string o = string(",") + only + ","; if (o.find("," + to_string(id) + ",") != string::npos) keep.push_back(id); } app.swap(keep); }
        const char *rt = getenv("HX_ROUTER"); Router R; int pass = 9; double pred_best = 0;
        bool use = rt && !getenv("HX_ALLROUTES") && !getenv("HX_ALLROUTES_MAX") && R.load(rt, getenv("HX_HOLDOUT") ? getenv("HX_HOLDOUT") : "");
        if (!use) { for (int id : app) run_route(id); }
        else {   // best three, then escalation keyed to the gap between predicted and actual size (Michael's rules: 3% / 10%)
            auto pr = R.predict(F, app); double mn = 1.0; for (auto &x : pr) mn = min(mn, x.first); pred_best = mn * base_best; size_t k = 0;
            bool novel = R.last_nn > R.novel_thr;   // far from everything seen before: explore every route (and learn it)
            auto runk = [&](size_t cnt) { for (size_t j = 0; j < cnt && k < pr.size(); j++, k++) run_route(pr[k].second); };
            if (novel) { runk(pr.size()); pass = 4; } else { runk(3); pass = 1; }
            if (best.size() > 1.03 * pred_best && k < pr.size()) { runk(3); pass = 2; }
            if (best.size() > 1.10 * pred_best && k < pr.size()) { runk(pr.size()); pass = 3; }
            if (dbg) { fprintf(stderr, "  router: predicted best %.0f, got %zu, pass %d; ranking", pred_best, best.size(), pass); for (auto &x : pr) fprintf(stderr, " %d:%.3f", x.second, x.first); fprintf(stderr, "\n"); } }
        if (const char *lg = getenv("HX_ROUTELOG")) {   // the learning loop: every file teaches the router (misses and surprises flagged)
            FILE *fl = fopen(lg, "a"); if (fl) { const char *st = getenv("HX_SET"); string fn = name; fn = fn.substr(fn.find_last_of('/') + 1);
                fprintf(fl, "%s,%s,%zu", st ? st : "-", fn.c_str(), n); for (double v : F) fprintf(fl, ",%.6g", v);
                const char *flag = pass == 9 ? "all" : pass == 4 ? "novel" : best.size() > 1.03 * pred_best ? "miss" : best.size() < 0.90 * pred_best ? "surprise" : "ok";
                fprintf(fl, ",%zu,%d,%zu,%d,%d:%s,%.0f,%s\n", base_best, base_mode, best.size(), mode, pass, flag, pred_best, rlog.c_str()); fclose(fl); } }
        }
        // x86 code with the repeat line (HX Fast and Complete): only when the repeat line is already the winner
        { B xf = in; size_t cv = x86_filter(xf.data(), n, true); if (cv * 1000 >= n && mode == 2) { B z = zstd_long(xf, T, 19); if (z.size() < best.size()) { best.swap(z); mode = 64 + 2; } } }
        if (best.size() >= n) { best = in; mode = 0; }
        B o = {'H', 'X', 'F', '1'}; for (int i = 0; i < 8; i++) o.push_back((uint64_t)n >> (8 * i) & 255); o.push_back((uint8_t)mode); o.insert(o.end(), best.begin(), best.end());
        int bm = mode & 63; fprintf(g_msg, "%shxf: %zu -> %zu bytes (%s%s), ratio %.3fx, %.2f s\n", g_prefix.c_str(), n, o.size(), mode & 64 ? "x86 filter + " : "", bm == 1 ? "Sets + Exchange lines" : bm == 2 ? "repeat line" : bm == 3 ? "split: Exchange lines + repeat line" : bm == 4 ? "BSC" : bm == 5 ? "bzip3" : bm == 6 ? "split: Exchange lines + BSC" : bm == 7 ? "split: Exchange lines + bzip3" : bm == 10 ? "format specialist" : bm == 11 ? "embedded images + HX Complete" : bm == 12 ? "LZMA" : bm == 13 ? "BCJ2 + LZMA/BSC" : bm == 14 ? "PPMd" : bm == 15 ? "Brotli 11" : bm == 16 ? "HCM max" : bm == 17 ? "HCM 3.7" : bm == 18 ? "16-bit image to JPEG XL" : bm == 8 ? "mzip (research)" : bm == 9 ? "split: Exchange lines + mzip (research)" : "stored", (double)n / max<size_t>(1, o.size()), now() - t0);
        return o;
}
static B decode_one(const uint8_t *data, size_t size, int T, const string &self) {
        if (size < 13 || memcmp(data, "HXF1", 4)) { fprintf(stderr, "hx: not an HX file\n"); exit(1); }
        uint64_t n = 0; for (int i = 0; i < 8; i++) n |= (uint64_t)data[4 + i] << (8 * i); int mode = data[12]; bool x86 = mode & 64; mode &= 63; const uint8_t *p = data + 13; size_t m = size - 13;
        B out(n);
        if (mode == 0) memcpy(out.data(), p, n);
        else if (mode == 1) hx_decode(p, m, out.data(), n, T);
        else if (mode == 2) { size_t r = ZSTD_decompress(out.data(), n, p, m); if (ZSTD_isError(r) || r != n) { fprintf(stderr, "hxf: zstd decode error\n"); exit(1); } }
        else if (mode == 4) bsc_dec(p, out.data(), n, T);
        else if (mode == 5) bz3_dec(p, m, out.data(), n);
        else if (mode == 12) { if (!hxlzma::dec(p, m, out.data(), n)) { fprintf(stderr, "hxf: lzma decode failed\n"); exit(1); } }
        else if (mode == 13) { if (!bcj2_unroute(p, m, out.data(), n, T)) { fprintf(stderr, "hxf: bcj2 decode failed\n"); exit(1); } }
        else if (mode == 14) { if (!hxppmd::dec(p, m, out.data(), n)) { fprintf(stderr, "hxf: ppmd decode failed\n"); exit(1); } }
        else if (mode == 15) { if (!brotli_unroute(p, m, out.data(), n)) { fprintf(stderr, "hxf: brotli decode failed\n"); exit(1); } }
        else if (mode == 16) { B r = hcm_run("d", p, m); if (r.size() != n) { fprintf(stderr, "hxf: this file needs HX_HCM (HCM program)\n"); exit(1); } memcpy(out.data(), r.data(), n); }
        else if (mode == 17) { B r = hcm37_unroute(p, m); if (r.size() != n) { fprintf(stderr, "hxf: this file needs HX_HCM37 / HX_KZF\n"); exit(1); } memcpy(out.data(), r.data(), n); }
        else if (mode == 18) { B r = img16_unroute(p, m, n); if (r.size() != n) { fprintf(stderr, "hxf: 16-bit image decode failed (needs HX_DJXL)\n"); exit(1); } memcpy(out.data(), r.data(), n); }
        else if (mode == 10) { B r = media_dec(p[0], p + 1, m - 1, n); if (r.size() != n) { fprintf(stderr, "hxf: media route decode failed\n"); exit(1); } memcpy(out.data(), r.data(), n); }
        else if (mode == 11) { size_t cnt = get32(p); p += 4; vector<size_t> off(cnt), len(cnt); vector<B> img(cnt);
            for (size_t k = 0; k < cnt; k++) { off[k] = get32(p); p += 4; len[k] = get32(p); p += 4; size_t bl = get32(p); p += 4;
                img[k] = media_dec(p[0], p + 1, bl - 1, len[k]); if (img[k].size() != len[k]) { fprintf(stderr, "hxf: embedded image decode failed\n"); exit(1); } p += bl; }
            size_t rl = get32(p); p += 4; B rest;
            if (rl) { string d = mz_tmp(), a = d + "/r.h", b = d + "/r"; writef(a.c_str(), p, rl);
                bool ok = sh(q(self) + " d " + q(a) + " " + q(b) + " " + to_string(T) + " > " HX_NULL); if (ok) rest = readf(b.c_str()); remove(a.c_str()); remove(b.c_str()); rmdir(d.c_str());
                if (!ok) { fprintf(stderr, "hxf: rest decode failed\n"); exit(1); } }
            size_t pos = 0, q2 = 0; for (size_t k = 0; k < cnt; k++) { size_t gap = off[k] - pos; memcpy(out.data() + pos, rest.data() + q2, gap); q2 += gap; memcpy(out.data() + off[k], img[k].data(), len[k]); pos = off[k] + len[k]; }
            memcpy(out.data() + pos, rest.data() + q2, n - pos); }
        else if (mode == 8) { B r = mz_run("d", p, m); if (r.size() != n) { fprintf(stderr, "hxf: mzip size mismatch\n"); exit(1); } memcpy(out.data(), r.data(), n); }
        else {   // split: the repeat-line stream on one core while the HX segments decode on the others
            Plan P; plan_segments(P, n); size_t ns = get32(p); p += 4; if (ns != P.soff.size()) { fprintf(stderr, "hxf: damaged file\n"); exit(1); }
            vector<char> mark(p, p + ns); p += ns; size_t zl = get32(p); p += 4; const uint8_t *zp = p; p += zl;
            vector<const uint8_t *> hpp(ns, nullptr); size_t restn = 0; for (size_t k = 0; k < ns; k++) { if (mark[k]) { size_t L = get32(p); p += 4; hpp[k] = p; p += L; } else restn += P.slen[k]; }
            B rest(restn); vector<size_t> hk; for (size_t k = 0; k < ns; k++) if (mark[k]) hk.push_back(k);
            thread zt([&]() { if (mode == 6) bsc_dec(zp, rest.data(), restn, 1); else if (mode == 7) bz3_dec(zp, zl, rest.data(), restn);
                else if (mode == 9) { B r = mz_run("d", zp, zl); if (r.size() != restn) { fprintf(stderr, "hxf: mzip size mismatch\n"); exit(1); } memcpy(rest.data(), r.data(), restn); }
                else { size_t r = ZSTD_decompress(rest.data(), restn, zp, zl); if (ZSTD_isError(r) || r != restn) { fprintf(stderr, "hxf: zstd decode error\n"); exit(1); } }
                size_t q = 0; for (size_t k = 0; k < ns; k++) if (!mark[k]) { memcpy(out.data() + P.soff[k], rest.data() + q, P.slen[k]); q += P.slen[k]; } });
            runpar(hk.size(), max(1, T - 1), [&](size_t i) { size_t k = hk[i]; hx_seg_dec(hpp[k], out.data() + P.soff[k], P.slen[k], HBLK); });
            zt.join(); }
        if (x86) x86_filter(out.data(), n, false);
        return out;
}
#ifdef _WIN32
#include <fcntl.h>
#endif
// --memlimit (HX 1.1.0): peak memory was measured at blocks of 16M-256M (HX Fast) and 16M-128M (HX Complete), 1-16 threads
// (HX/notes/HX-1.1.0-memory.txt). Every measured peak lies on or under the line
//   peak (MiB) = BASE + PER_BYTE x block (MiB) + PER_THREAD x threads
// so --memlimit gives the block (limit - BASE - PER_THREAD x threads) / PER_BYTE, rounded down to whole MiB, and never more
// than the largest block measured (256M Fast, 128M Complete).
static const double MEM_BASE_FAST = 160, MEM_PER_BYTE_FAST = 4.75, MEM_BASE_COMPLETE = 640, MEM_PER_BYTE_COMPLETE = 12, MEM_PER_THREAD = 26;
static const size_t MEM_MAX_BLOCK_FAST = (size_t)256 << 20, MEM_MAX_BLOCK_COMPLETE = (size_t)128 << 20;
static const size_t MIN_BLOCK = (size_t)1 << 20, MAX_BLOCK = (size_t)2047 << 20, PIPE_BLOCK = (size_t)256 << 20;
static bool parse_size(const char *s, size_t &v) {   // 1048576, 512K, 256M, 8G (binary units; a trailing B or iB is allowed)
    char *e; double x = strtod(s, &e); if (e == s || x <= 0) return false;
    string u(e); for (auto &c : u) c = toupper(c); if (u == "B" || u.empty()) {} else if (u[0] == 'K') x *= 1024.0; else if (u[0] == 'M') x *= 1048576.0; else if (u[0] == 'G') x *= 1073741824.0; else return false;
    if (u.size() > 1 && u.substr(1) != "B" && u.substr(1) != "IB") return false;
    v = (size_t)x; return true;
}
static size_t read_full(FILE *f, uint8_t *d, size_t n) { size_t g = 0; while (g < n) { size_t r = fread(d + g, 1, n - g, f); if (!r) break; g += r; } return g; }
static void write_all(FILE *f, const uint8_t *d, size_t n) { if (n && fwrite(d, 1, n, f) != n) { fprintf(stderr, "hx: write error\n"); exit(1); } }
static void put64f(FILE *f, uint64_t v) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = v >> (8 * i) & 255; write_all(f, b, 8); }
static bool get64f(FILE *f, uint64_t &v) { uint8_t b[8]; if (read_full(f, b, 8) != 8) return false; v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)b[i] << (8 * i); return true; }
static string mib(size_t v) { char b[32]; if (v % ((size_t)1 << 20) == 0) snprintf(b, sizeof b, "%zuM", v >> 20); else snprintf(b, sizeof b, "%zu bytes", v); return b; }
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "HX 1.1.0 (Harrington Sets + Exchange lines + cascade + learned router)\n"
        "usage: hx c IN OUT [options]   |   hx d IN OUT [options]      (IN or OUT can be - for stdin / stdout)\n"
        "  --complete       HX Complete (every route, smallest size, slow compress)\n"
        "  --block=SIZE     compress in independent blocks of SIZE (e.g. 64M, 256M, 1G); memory follows the block, not the file\n"
        "  --memlimit=SIZE  choose the block size so compression stays under SIZE of memory (e.g. 8G)\n"
        "  -T N, --threads=N  threads (default: all logical cores); a plain number also sets the threads\n"
        "  --max            router off: try every route (smallest size, slowest compress)\n"
        "  --no-learn       do not add this file to the router's learning table\n"
        "Without --block / --memlimit / a pipe, the output is the HX 1.0 format, unchanged. Reading a pipe uses 256M blocks\n"
        "unless --block or --memlimit says otherwise. HX 1.1 decodes every HX 1.0 file.\n"); return 2; }
    int T = (int)max(1u, thread::hardware_concurrency()); g_probe = 131072; bool complete = false; bsc_init(LIBBSC_FEATURE_NONE); if (getenv("HX_SLICES")) g_slices = max(1, atoi(getenv("HX_SLICES")));
    size_t blk = 0, memlim = 0;
    auto need = [&](int &a, const char *flag) -> const char * { if (a + 1 >= argc) { fprintf(stderr, "hx: %s needs a value\n", flag); exit(2); } return argv[++a]; };
    auto size_arg = [&](const char *v, const char *flag) { size_t x; if (!parse_size(v, x)) { fprintf(stderr, "hx: bad size for %s: %s (use e.g. 256M or 8G)\n", flag, v); exit(2); } return x; };
    for (int a = 4; a < argc; a++) { const char *s = argv[a];
        if (!strcmp(s, "--full")) g_probe = 0; else if (!strcmp(s, "--complete")) complete = true; else if (!strcmp(s, "--max")) setenv("HX_ALLROUTES_MAX", "1", 1); else if (!strcmp(s, "--no-learn")) unsetenv("HX_ROUTELOG"); else if (!strcmp(s, "--probe") && a + 1 < argc) g_probe = (size_t)atoi(argv[++a]) * 1024;
        else if (!strcmp(s, "--abl-pick1")) g_keepmax = 1; else if (!strcmp(s, "--abl-lines-off")) abl_lines_off = true;
        else if (!strcmp(s, "--abl-sets-off")) abl_sets_off = true; else if (!strcmp(s, "--abl-repeat-off")) abl_repeat_off = true;
        else if (!strcmp(s, "--abl-split-off")) abl_split_off = true; else if (!strcmp(s, "--abl-gmap") && a + 1 < argc) abl_gmap = argv[++a];
        else if (!strncmp(s, "--block=", 8)) blk = size_arg(s + 8, "--block"); else if (!strcmp(s, "--block") || !strcmp(s, "-b")) blk = size_arg(need(a, s), "--block");
        else if (!strncmp(s, "--memlimit=", 11)) memlim = size_arg(s + 11, "--memlimit"); else if (!strcmp(s, "--memlimit") || !strcmp(s, "-M")) memlim = size_arg(need(a, s), "--memlimit");
        else if (!strncmp(s, "--threads=", 10)) T = max(1, atoi(s + 10)); else if (!strcmp(s, "-T") || !strcmp(s, "--threads")) T = max(1, atoi(need(a, s)));
        else if (s[0] == '-' && s[1] == 'T' && isdigit((unsigned char)s[2])) T = max(1, atoi(s + 2));
        else if (isdigit((unsigned char)s[0])) T = max(1, atoi(s));
        else { fprintf(stderr, "hx: unknown option %s\n", s); return 2; } }
    const string self = argv[0]; bool in_std = !strcmp(argv[2], "-"), out_std = !strcmp(argv[3], "-");
#ifdef _WIN32
    if (in_std) _setmode(_fileno(stdin), _O_BINARY);
    if (out_std) _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (out_std) g_msg = stderr;
    string name = argv[2]; { size_t sl = name.find_last_of("/\\"); if (sl != string::npos) name = name.substr(sl + 1); }
    double t0 = now();
    if (argv[1][0] == 'c') {
        if (blk && memlim) { fprintf(stderr, "hx: use --block or --memlimit, not both\n"); return 2; }
        if (memlim) { double base = complete ? MEM_BASE_COMPLETE : MEM_BASE_FAST, per = complete ? MEM_PER_BYTE_COMPLETE : MEM_PER_BYTE_FAST;
            double room = memlim / 1048576.0 - base - MEM_PER_THREAD * T; blk = room > 0 ? (size_t)(room / per) << 20 : 0; blk = min(blk, complete ? MEM_MAX_BLOCK_COMPLETE : MEM_MAX_BLOCK_FAST);
            if (blk < MIN_BLOCK) { fprintf(stderr, "hx: --memlimit %s is too small for %d threads; use a larger limit or fewer threads (-T)\n", mib(memlim).c_str(), T); return 2; } }
        if (!blk && in_std) blk = PIPE_BLOCK;
        if (blk) blk = min(max(blk, MIN_BLOCK), MAX_BLOCK);
        FILE *fi = in_std ? stdin : fopen(argv[2], "rb"); if (!fi) { perror(argv[2]); return 1; }
        FILE *fo = out_std ? stdout : fopen(argv[3], "wb"); if (!fo) { perror(argv[3]); return 1; }
        if (!blk) {   // HX 1.0 format: the whole file is one HXF1 record
            fclose(fi); fi = nullptr; B in = readf(argv[2]); B o = compress_one(in, T, complete, self, name); write_all(fo, o.data(), o.size());
        } else {
            fprintf(g_msg, "hx: block mode, blocks of %s%s, %d threads\n", mib(blk).c_str(), memlim ? (" (from --memlimit " + mib(memlim) + ")").c_str() : "", T);
            write_all(fo, (const uint8_t *)"HXB1", 4); uint8_t ver = 1; write_all(fo, &ver, 1); put64f(fo, blk);
            uint64_t total = 0, outn = 13; size_t k = 0;
            for (;; k++) { B buf(blk); size_t n = read_full(fi, buf.data(), blk); if (!n) break; buf.resize(n);
                g_prefix = "block " + to_string(k + 1) + ": "; B o = compress_one(buf, T, complete, self, name); B().swap(buf);
                put64f(fo, o.size()); write_all(fo, o.data(), o.size()); total += n; outn += 8 + o.size(); if (n < blk) { k++; break; } }
            put64f(fo, 0); put64f(fo, total); outn += 16; g_prefix.clear();
            fprintf(g_msg, "hx: %llu -> %llu bytes in %zu block%s, ratio %.3fx, %.2f s\n", (unsigned long long)total, (unsigned long long)outn, k, k == 1 ? "" : "s", (double)total / max<uint64_t>(1, outn), now() - t0);
        }
        if (fi && fi != stdin) fclose(fi);
        if (fflush(fo) || (fo != stdout && fclose(fo))) { fprintf(stderr, "hx: write error\n"); return 1; }
    } else {
        FILE *fi = in_std ? stdin : fopen(argv[2], "rb"); if (!fi) { perror(argv[2]); return 1; }
        uint8_t mg[4]; if (read_full(fi, mg, 4) != 4) { fprintf(stderr, "hx: not an HX file\n"); return 1; }
        FILE *fo = out_std ? stdout : fopen(argv[3], "wb"); if (!fo) { perror(argv[3]); return 1; }
        uint64_t total = 0;
        if (!memcmp(mg, "HXF1", 4)) {   // HX 1.0 format
            B in(mg, mg + 4); { uint8_t b[1 << 16]; size_t r; while ((r = fread(b, 1, sizeof b, fi)) > 0) in.insert(in.end(), b, b + r); }
            B out = decode_one(in.data(), in.size(), T, self); B().swap(in); write_all(fo, out.data(), out.size()); total = out.size();
        } else if (!memcmp(mg, "HXB1", 4)) {
            uint8_t ver; uint64_t bs, len; if (read_full(fi, &ver, 1) != 1 || ver != 1 || !get64f(fi, bs)) { fprintf(stderr, "hx: unknown HX block format version\n"); return 1; }
            for (;;) { if (!get64f(fi, len)) { fprintf(stderr, "hx: damaged or cut-off file\n"); return 1; }
                if (!len) { uint64_t want; if (!get64f(fi, want) || want != total) { fprintf(stderr, "hx: damaged file (total size does not match)\n"); return 1; } break; }
                if (len > bs + bs / 8 + ((size_t)1 << 20)) { fprintf(stderr, "hx: damaged file (block too long)\n"); return 1; }
                B c(len); if (read_full(fi, c.data(), len) != len) { fprintf(stderr, "hx: damaged or cut-off file\n"); return 1; }
                B out = decode_one(c.data(), c.size(), T, self); if (out.size() > bs) { fprintf(stderr, "hx: damaged file\n"); return 1; }
                write_all(fo, out.data(), out.size()); total += out.size(); }
        } else { fprintf(stderr, "hx: not an HX file\n"); return 1; }
        if (fi != stdin) fclose(fi);
        if (fflush(fo) || (fo != stdout && fclose(fo))) { fprintf(stderr, "hx: write error\n"); return 1; }
        fprintf(g_msg, "hxf: decoded %llu bytes in %.4f s\n", (unsigned long long)total, now() - t0);
    }
    return 0;
}
