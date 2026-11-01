// hs_wrap.cpp version 1.4 - Harrington Sets wrapper, streaming C++ version (same HSW2 file format as
// hs_wrap.py 1.2 and the in-memory C++ port: output files are byte-identical and interchangeable).
// Runs any single-core compressor through Harrington Sets: 64 KB windows, entropy exclusion above
// 7.99 bits/byte, k-means into at most 3 sets, run smoothing, blocks of at most B MB, one compressor copy
// per block on separate cores (largest first), run-length map, CRC-32 of the original.
// 1.3: the input is streamed from disk in two passes and never held in memory, so files far larger than RAM
// (tested to 10 GB on a 16 GB machine) work; memory use is a few MB plus the compressor copies.
// 1.4: direct decompression. When the --decomp template has neither {in} nor {out} (e.g. "gzip -d -c"),
// each block is piped straight from the container into a decoder's standard input and its standard output is
// read straight into memory - no temporary files. Blocks are decoded on all cores in the order the output needs
// them, and one writer streams the output front to back (no out-of-order writes, so no zero-filling on NTFS).
// Memory is bounded: at most max(jobs + 2, 2 x sets + 2) decoded blocks are held at once (--hold N to change). The CRC-32 is computed on the
// worker cores per piece and joined with crc32_combine, so the writer only writes. Same file format as 1.3.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Usage:
//   hs_wrap c IN OUT --comp "COMPRESSOR {in} {out}" [--jobs N] [--block-mb B]
//   hs_wrap d IN OUT --decomp "DECOMPRESSOR {in} {out}" [--jobs N]
// {in}/{out} become temporary block files in a folder next to OUT (removed afterwards); a template without
// {out} is read from standard output. Temporary space needed: about the input size plus the output size.
// Build: g++ -O2 -std=c++17 -pthread hs_wrap14.cpp -o hs_wrap
//        zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_wrap14.cpp -o hs_wrap.exe
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
#define FSEEK _fseeki64
#else
#define FSEEK fseeko
#endif
static const size_t W = 65536; static const int RAW = 255;

// ---- CRC-32 (zlib polynomial), slicing by 8 ----
static uint32_t T8[8][256];
static void crc_init() {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T8[0][i] = c; }
    for (int t = 1; t < 8; t++) for (int i = 0; i < 256; i++) T8[t][i] = (T8[t - 1][i] >> 8) ^ T8[0][T8[t - 1][i] & 255];
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, size_t n) {   // c = running value (start 0)
    c = ~c;
    while (n >= 8) {
        uint32_t a = c ^ (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24), b = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        c = T8[7][a & 255] ^ T8[6][(a >> 8) & 255] ^ T8[5][(a >> 16) & 255] ^ T8[4][a >> 24] ^ T8[3][b & 255] ^ T8[2][(b >> 8) & 255] ^ T8[1][(b >> 16) & 255] ^ T8[0][b >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = T8[0][(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

// ---- small file helpers ----
struct F { FILE *f = nullptr; F(const string &p, const char *m) { f = fopen(p.c_str(), m); if (!f) throw runtime_error("cannot open " + p); setvbuf(f, nullptr, _IOFBF, 1 << 20); } ~F() { if (f) fclose(f); } };
static void wr(FILE *f, const void *p, size_t n) { if (n && fwrite(p, 1, n, f) != n) throw runtime_error("write failed (disk full?)"); }
static void rd(FILE *f, void *p, size_t n) { if (n && fread(p, 1, n, f) != n) throw runtime_error("truncated or damaged file"); }
template <class T> static void put(FILE *f, T v) { wr(f, &v, sizeof v); }
template <class T> static T get(FILE *f) { T v; rd(f, &v, sizeof v); return v; }
static void copy_n(FILE *from, FILE *to, uint64_t n) { vector<uint8_t> b(1 << 20); while (n) { size_t k = (size_t)min<uint64_t>(n, b.size()); rd(from, b.data(), k); wr(to, b.data(), k); n -= k; } }
static uint64_t fsize(const string &p) { error_code ec; auto s = fs::file_size(p, ec); if (ec) throw runtime_error("missing file " + p); return s; }

// ---- labelling (same rules as hs_wrap.py) ----
static void features(const uint8_t *c, size_t L, array<double, 7> &F, double &e) {
    double h[256] = {0}; for (size_t i = 0; i < L; i++) h[c[i]]++;
    e = 0; for (int i = 0; i < 256; i++) if (h[i] > 0) { double p = h[i] / L; e -= p * log2(p); }
    double alpha = 0, digit = 0, high = 0, ctl = 0;
    for (int i = 65; i < 91; i++) alpha += h[i]; for (int i = 97; i < 123; i++) alpha += h[i];
    for (int i = 48; i < 58; i++) digit += h[i]; for (int i = 128; i < 256; i++) high += h[i];
    for (int i = 0; i < 32; i++) ctl += h[i]; ctl += h[127] - h[10] - h[13] - h[9];
    double space = h[32] + h[10] + h[13] + h[9];
    F = {ctl / L, alpha / L, digit / L, space / L, high / L, h[0] / L, e / 8};
}
static vector<int> labels(const vector<array<double, 7>> &F, const vector<double> &E, const vector<size_t> &Ls) {
    size_t nw = F.size(); vector<int> lab(nw, 0); vector<size_t> idx;
    for (size_t i = 0; i < nw; i++) { if (E[i] > 7.99 && Ls[i] >= 4096) lab[i] = RAW; else idx.push_back(i); }
    int K = idx.size() >= 48 ? 3 : (idx.size() >= 16 ? 2 : 1);
    if (!idx.empty()) {
        size_t m = idx.size(); auto d2 = [&](const array<double, 7> &a, const array<double, 7> &b) { double s = 0; for (int k = 0; k < 7; k++) s += (a[k] - b[k]) * (a[k] - b[k]); return s; };
        vector<array<double, 7>> C{F[idx[0]]};
        for (int k = 1; k < K; k++) {
            size_t best = 0; double bd = -1;
            for (size_t j = 0; j < m; j++) { double d = 1e300; for (auto &c : C) d = min(d, d2(F[idx[j]], c)); if (d > bd) { bd = d; best = j; } }
            C.push_back(F[idx[best]]);
        }
        vector<int> a(m);
        for (int it = 0; it < 20; it++) {
            for (size_t j = 0; j < m; j++) { int b = 0; double bd = d2(F[idx[j]], C[0]); for (int k = 1; k < K; k++) { double d = d2(F[idx[j]], C[k]); if (d < bd) { bd = d; b = k; } } a[j] = b; }
            for (int k = 0; k < K; k++) {
                array<double, 7> s{}; size_t c = 0;
                for (size_t j = 0; j < m; j++) if (a[j] == k) { for (int q = 0; q < 7; q++) s[q] += F[idx[j]][q]; c++; }
                if (c) for (int q = 0; q < 7; q++) C[k][q] = s[q] / c;
            }
        }
        vector<size_t> cnt(K, 0); for (int x : a) cnt[x]++;
        int big = (int)(max_element(cnt.begin(), cnt.end()) - cnt.begin());
        for (size_t j = 0; j < m; j++) lab[idx[j]] = cnt[a[j]] * W < (1u << 20) ? big : a[j];
    }
    for (int pass = 0; pass < 2; pass++) {
        size_t i = 0;
        while (i < nw) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; if (j - i < 8 && i > 0) for (size_t q = i; q < j; q++) lab[q] = lab[i - 1]; i = j; }
    }
    return lab;
}

// ---- running the compressor ----
static string quote(const string &s) { return "\"" + s + "\""; }
static void call(const string &tmpl, const string &src, const string &dst) {
    string cmd = tmpl; bool hasout = tmpl.find("{out}") != string::npos;
    for (size_t p; (p = cmd.find("{in}")) != string::npos;) cmd.replace(p, 4, quote(src));
    for (size_t p; (p = cmd.find("{out}")) != string::npos;) cmd.replace(p, 5, quote(dst));
#ifdef _WIN32
    cmd += hasout ? " >NUL 2>NUL" : " >" + quote(dst) + " 2>NUL";
    cmd = "\"" + cmd + "\"";   // cmd /c strips one outer pair of quotes
#else
    cmd += hasout ? " >/dev/null 2>&1" : " >" + quote(dst) + " 2>/dev/null";
#endif
    int r = system(cmd.c_str());
    if (r != 0) throw runtime_error("compressor failed (exit " + to_string(r) + "): " + tmpl);
}
static void run_all(const string &tmpl, const vector<pair<string, string>> &pairs, const vector<uint64_t> &sizes, int jobs) {
    vector<size_t> order(pairs.size()); for (size_t i = 0; i < order.size(); i++) order[i] = i;
    stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sizes[a] > sizes[b]; });
    atomic<size_t> next{0}; mutex mu; string err;
    auto worker = [&] { for (size_t k; (k = next++) < order.size();) { try { call(tmpl, pairs[order[k]].first, pairs[order[k]].second); } catch (exception &e) { lock_guard<mutex> g(mu); if (err.empty()) err = e.what(); } } };
    vector<thread> th; for (int i = 0; i < max(1, jobs); i++) th.emplace_back(worker); for (auto &t : th) t.join();
    if (!err.empty()) throw runtime_error(err);
}

// ---- 1.4: decoder processes with pipes (no shell, no temporary files) ----
#ifdef _WIN32
struct Child { HANDLE in = nullptr, out = nullptr, proc = nullptr; };
static mutex spawn_mu;
static Child spawn_pipe(const string &cmdline) {
    Child c; SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; HANDLE ir, iw, orr, ow;
    lock_guard<mutex> g(spawn_mu);   // child ends are inheritable only inside this lock, so no child gets another's pipe
    if (!CreatePipe(&ir, &iw, &sa, 1 << 20) || !CreatePipe(&orr, &ow, &sa, 1 << 20)) throw runtime_error("cannot create pipe");
    SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(orr, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOA si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = ir; si.hStdOutput = ow; si.hStdError = nul;
    PROCESS_INFORMATION pi{}; vector<char> b(cmdline.begin(), cmdline.end()); b.push_back(0);
    BOOL ok = CreateProcessA(nullptr, b.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(ir); CloseHandle(ow); CloseHandle(nul);
    if (!ok) { CloseHandle(iw); CloseHandle(orr); throw runtime_error("cannot start decoder: " + cmdline); }
    CloseHandle(pi.hThread); c.in = iw; c.out = orr; c.proc = pi.hProcess; return c;
}
static bool pwrite_all(Child &c, const uint8_t *p, size_t n) { while (n) { DWORD w = 0; if (!WriteFile(c.in, p, (DWORD)min<size_t>(n, 1 << 20), &w, nullptr) || !w) return false; p += w; n -= w; } return true; }
static void close_in(Child &c) { if (c.in) { CloseHandle(c.in); c.in = nullptr; } }
static size_t pread_some(Child &c, uint8_t *p, size_t n) { DWORD r = 0; if (!ReadFile(c.out, p, (DWORD)min<size_t>(n, 1 << 24), &r, nullptr)) return 0; return r; }
static int finish(Child &c) { close_in(c); CloseHandle(c.out); WaitForSingleObject(c.proc, INFINITE); DWORD code = 1; GetExitCodeProcess(c.proc, &code); CloseHandle(c.proc); return (int)code; }
#else
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
extern char **environ;
struct Child { int in = -1, out = -1; pid_t pid = 0; };
static Child spawn_pipe(const string &cmdline) {
    signal(SIGPIPE, SIG_IGN); int a[2], b[2]; if (pipe2(a, O_CLOEXEC) || pipe2(b, O_CLOEXEC)) throw runtime_error("cannot create pipe");
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa); posix_spawn_file_actions_adddup2(&fa, a[0], 0); posix_spawn_file_actions_adddup2(&fa, b[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    const char *argv[] = {"/bin/sh", "-c", cmdline.c_str(), nullptr}; Child c;
    if (posix_spawn(&c.pid, "/bin/sh", &fa, nullptr, (char **)argv, environ)) throw runtime_error("cannot start decoder: " + cmdline);
    posix_spawn_file_actions_destroy(&fa); close(a[0]); close(b[1]); c.in = a[1]; c.out = b[0]; return c;
}
static bool pwrite_all(Child &c, const uint8_t *p, size_t n) { while (n) { ssize_t w = write(c.in, p, n); if (w <= 0) return false; p += w; n -= w; } return true; }
static void close_in(Child &c) { if (c.in >= 0) { close(c.in); c.in = -1; } }
static size_t pread_some(Child &c, uint8_t *p, size_t n) { ssize_t r = read(c.out, p, n); return r > 0 ? (size_t)r : 0; }
static int finish(Child &c) { close_in(c); close(c.out); int st = 0; waitpid(c.pid, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 1; }
#endif
// zlib's crc32_combine: CRC of A+B from CRC(A), CRC(B) and len(B)
static uint32_t gf2_times(const uint32_t *m, uint32_t v) { uint32_t s = 0; while (v) { if (v & 1) s ^= *m; v >>= 1; m++; } return s; }
static void gf2_square(uint32_t *sq, const uint32_t *m) { for (int n = 0; n < 32; n++) sq[n] = gf2_times(m, m[n]); }
static uint32_t crc_combine(uint32_t c1, uint32_t c2, uint64_t len2) {
    if (!len2) return c1; uint32_t even[32], odd[32]; odd[0] = 0xEDB88320u; uint32_t row = 1; for (int n = 1; n < 32; n++) { odd[n] = row; row <<= 1; }
    gf2_square(even, odd); gf2_square(odd, even);
    do { gf2_square(even, odd); if (len2 & 1) c1 = gf2_times(even, c1); len2 >>= 1; if (!len2) break; gf2_square(odd, even); if (len2 & 1) c1 = gf2_times(odd, c1); len2 >>= 1; } while (len2);
    return c1 ^ c2;
}
struct TmpDir { string path; TmpDir(const string &out) { path = out + ".hsw_tmp"; fs::remove_all(path); fs::create_directories(path); } ~TmpDir() { error_code ec; fs::remove_all(path, ec); } string at(const string &n) const { return (fs::path(path) / n).string(); } };

static void compress(const string &in, const string &out, const string &comp, int jobs, long block_mb) {
    // pass 1: window features and CRC, streamed
    vector<array<double, 7>> Fv; vector<double> E; vector<size_t> Ls; uint32_t crc = 0; uint64_t n = 0;
    { F fi(in, "rb"); vector<uint8_t> b(W); size_t k;
      while ((k = fread(b.data(), 1, W, fi.f)) > 0) { array<double, 7> f; double e; features(b.data(), k, f, e); Fv.push_back(f); E.push_back(e); Ls.push_back(k); crc = crc_upd(crc, b.data(), k); n += k; if (k < W) break; } }
    auto lab = labels(Fv, E, Ls); size_t nw = lab.size();
    vector<pair<int, uint32_t>> R; for (size_t i = 0; i < nw;) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++; R.push_back({lab[i], (uint32_t)(j - i)}); i = j; }
    set<int> S(lab.begin(), lab.end());
    map<int, uint64_t> setlen; for (size_t i = 0; i < nw; i++) setlen[lab[i]] += Ls[i];
    // block plan per set: same split as the Python version (nb blocks of ceil(len/nb), last one shorter)
    struct Blk { int s; uint64_t len; }; vector<Blk> blocks; map<int, uint64_t> per; map<int, size_t> first;
    for (int s : S) { if (s == RAW) continue; uint64_t L = setlen[s], B = (uint64_t)block_mb << 20, nb = max<uint64_t>(1, (L + B - 1) / B), p = (L + nb - 1) / nb;
        per[s] = p; first[s] = blocks.size(); for (uint64_t k = 0; k < nb; k++) blocks.push_back({s, min(L, (k + 1) * p) - min(L, k * p)}); }
    TmpDir td(out); vector<pair<string, string>> pairs; vector<uint64_t> sizes;
    for (size_t k = 0; k < blocks.size(); k++) { pairs.push_back({td.at("b" + to_string(k)), td.at("b" + to_string(k) + ".z")}); sizes.push_back(blocks[k].len); }
    // pass 2: split the file into block files and the raw file, streamed
    { F fi(in, "rb"); F fraw(td.at("raw"), "wb"); vector<uint8_t> b(W);
      map<int, uint64_t> pos; map<int, FILE *> cur; map<int, size_t> curk;
      for (size_t i = 0; i < nw; i++) {
          size_t k = Ls[i]; rd(fi.f, b.data(), k); int s = lab[i];
          if (s == RAW) { wr(fraw.f, b.data(), k); continue; }
          size_t off = 0;
          while (off < k) {
              size_t bk = first[s] + (size_t)(pos[s] / per[s]); uint64_t room = (bk - first[s] + 1) * per[s] - pos[s];
              if (!cur.count(s) || curk[s] != bk) { if (cur.count(s)) fclose(cur[s]); cur[s] = fopen(pairs[bk].first.c_str(), "wb"); if (!cur[s]) throw runtime_error("cannot write temporary file"); curk[s] = bk; }
              size_t t = (size_t)min<uint64_t>(room, k - off); wr(cur[s], b.data() + off, t); off += t; pos[s] += t;
          }
      }
      for (auto &c : cur) fclose(c.second); }
    for (auto &p : pairs) if (!fs::exists(p.first)) { F e(p.first, "wb"); }   // empty block (cannot happen, kept safe)
    run_all(comp, pairs, sizes, jobs);
    { F fo(out, "wb"); wr(fo.f, "HSW2", 4);
      put<uint64_t>(fo.f, n); put<uint32_t>(fo.f, crc); put<uint32_t>(fo.f, (uint32_t)R.size()); put<uint32_t>(fo.f, (uint32_t)blocks.size());
      for (auto &r : R) { put<uint8_t>(fo.f, (uint8_t)r.first); put<uint32_t>(fo.f, r.second); }
      for (size_t k = 0; k < blocks.size(); k++) { put<uint8_t>(fo.f, (uint8_t)blocks[k].s); put<uint64_t>(fo.f, blocks[k].len); put<uint64_t>(fo.f, fsize(pairs[k].second)); }
      uint64_t rl = setlen.count(RAW) ? setlen[RAW] : 0; put<uint64_t>(fo.f, rl);
      { F fr(td.at("raw"), "rb"); copy_n(fr.f, fo.f, rl); }
      for (auto &p : pairs) { F fz(p.second, "rb"); copy_n(fz.f, fo.f, fsize(p.second)); } }
    printf("%llu -> %llu bytes; sets", (unsigned long long)n, (unsigned long long)fsize(out)); for (int s : S) printf(" %d", s);
    printf(", runs %zu, blocks %zu, raw %llu\n", R.size(), blocks.size(), (unsigned long long)(setlen.count(RAW) ? setlen[RAW] : 0));
}

static long g_hold = 0;   // --hold N: max decoded blocks held in memory (0 = the larger of jobs + 2 and 2 x sets + 2; tested 2026-10-31: same speed, 25% less memory)
static void decompress(const string &in, const string &out, const string &decomp, int jobs) {
    uint64_t total = fsize(in); F fi(in, "rb"); char mg[4]; rd(fi.f, mg, 4);
    uint64_t n; uint32_t crc = 0, nr, nbk; bool hascrc;
    if (!memcmp(mg, "HSW2", 4)) { n = get<uint64_t>(fi.f); crc = get<uint32_t>(fi.f); nr = get<uint32_t>(fi.f); nbk = get<uint32_t>(fi.f); hascrc = true; }
    else if (!memcmp(mg, "HSW1", 4)) { n = get<uint64_t>(fi.f); nr = get<uint32_t>(fi.f); nbk = get<uint32_t>(fi.f); hascrc = false; }
    else throw runtime_error("not a Harrington Sets wrapper file");
    if ((uint64_t)nr * 5 + (uint64_t)nbk * 17 > total) throw runtime_error("truncated or damaged file");
    vector<pair<int, uint32_t>> R(nr); for (auto &r : R) { r.first = get<uint8_t>(fi.f); r.second = get<uint32_t>(fi.f); }
    struct Bk { int s; uint64_t ln, zl; }; vector<Bk> B(nbk); for (auto &b : B) { b.s = get<uint8_t>(fi.f); b.ln = get<uint64_t>(fi.f); b.zl = get<uint64_t>(fi.f); }
    uint64_t rl = get<uint64_t>(fi.f); uint64_t p = 4 + (hascrc ? 20 : 16) + 5ull * nr + 17ull * nbk + 8;
    if (rl > total - p) throw runtime_error("bad raw length");
    uint64_t rawpos = p; p += rl;
    // validate the container against the map before running any decoder
    uint64_t nw = (n + W - 1) / W, i = 0; map<int, uint64_t> need;
    for (auto &r : R) {
        if (r.first != RAW && r.first > 2) throw runtime_error("bad set label in the map");
        if (r.second == 0 || i + r.second > nw) throw runtime_error("the map does not match the file length");
        need[r.first] += (uint64_t)r.second * W - (i + r.second == nw ? nw * W - n : 0); i += r.second;
    }
    if (i != nw) throw runtime_error("the map does not match the file length");
    if (rl != (need.count(RAW) ? need[RAW] : 0)) throw runtime_error("bad raw length");
    map<int, uint64_t> got, want; uint64_t ztot = 0;
    for (auto &b : B) { if (b.s == RAW) throw runtime_error("block table does not match the map"); got[b.s] += b.ln; ztot += b.zl; }
    for (auto &kv : need) if (kv.first != RAW) want[kv.first] = kv.second;
    if (got != want) throw runtime_error("block table does not match the map");
    if (p + ztot != total) throw runtime_error("wrong file length (truncated or damaged)");
    if (decomp.find("{in}") == string::npos && decomp.find("{out}") == string::npos) {   // 1.4 direct path
        size_t nb = B.size(); vector<uint64_t> zoff(nb); { uint64_t q = p; for (size_t k = 0; k < nb; k++) { zoff[k] = q; q += B[k].zl; } }
        // segments in output order: (block or RAW, offset in block / in raw area, length); every output byte once
        struct Seg { int64_t blk; uint64_t off, len; uint32_t crc; }; vector<Seg> seg;
        map<int, vector<size_t>> bl; for (size_t k = 0; k < nb; k++) bl[B[k].s].push_back(k);
        map<int, pair<size_t, uint64_t>> cur;   // set -> (index into bl[set], offset in that block)
        vector<uint64_t> firstneed(nb, UINT64_MAX); vector<vector<size_t>> segs_of(nb);
        uint64_t outpos = 0, rawoff = 0, wi = 0;
        for (auto &r : R) {
            uint64_t L = min<uint64_t>((uint64_t)r.second * W, n - wi * W); wi += r.second;
            if (r.first == RAW) { seg.push_back({-1, rawoff, L, 0}); rawoff += L; outpos += L; continue; }
            auto &c = cur[r.first]; auto &lst = bl[r.first];
            while (L) { if (c.first >= lst.size()) throw runtime_error("set data ran out"); size_t k = lst[c.first]; uint64_t t = min<uint64_t>(L, B[k].ln - c.second);
                if (firstneed[k] == UINT64_MAX) firstneed[k] = outpos; segs_of[k].push_back(seg.size()); seg.push_back({(int64_t)k, c.second, t, 0});
                c.second += t; L -= t; outpos += t; if (c.second == B[k].ln) { c.first++; c.second = 0; } }
        }
        for (size_t k = 0; k < nb; k++) if (firstneed[k] == UINT64_MAX) firstneed[k] = outpos;   // (empty block)
        vector<size_t> order(nb); for (size_t k = 0; k < nb; k++) order[k] = k;
        stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return firstneed[a] < firstneed[b]; });
        struct Slot { vector<uint8_t> d; bool ready = false; }; vector<Slot> slot(nb);
        mutex mu; condition_variable cv; size_t held = 0; const size_t LIMIT = g_hold > 0 ? (size_t)max<long>(g_hold, (long)bl.size() + 1) : max((size_t)max(1, jobs) + 2, 2 * bl.size() + 2); string err; atomic<size_t> nx{0};
        auto worker = [&] {
            FILE *cf = fopen(in.c_str(), "rb"); if (!cf) { lock_guard<mutex> g(mu); err = "cannot open " + in; cv.notify_all(); return; }
            vector<uint8_t> zb(1 << 20);
            for (;;) {
                { unique_lock<mutex> g(mu); cv.wait(g, [&] { return held < LIMIT || !err.empty(); }); if (!err.empty()) break; size_t j = nx.load(); if (j >= nb) break; nx++; held++; g.unlock();
                  size_t k = order[j]; vector<uint8_t> d(B[k].ln); string e;
                  try {
                      Child ch = spawn_pipe(decomp); thread feed([&] { FSEEK(cf, (long long)zoff[k], SEEK_SET); uint64_t left = B[k].zl; bool ok = true;
                          while (left && ok) { size_t t = (size_t)min<uint64_t>(left, zb.size()); if (fread(zb.data(), 1, t, cf) != t) { ok = false; break; } ok = pwrite_all(ch, zb.data(), t); left -= t; } close_in(ch); });
                      size_t got = 0; vector<uint8_t> extra(1 << 16);
                      for (;;) { size_t r = got < d.size() ? pread_some(ch, d.data() + got, d.size() - got) : pread_some(ch, extra.data(), extra.size()); if (!r) break; if (got < d.size()) got += r; else { e = "block decoded longer than expected"; } }
                      feed.join(); int code = finish(ch);
                      if (code) e = "decoder failed (exit " + to_string(code) + "): " + decomp; else if (got != d.size() && e.empty()) e = "block length mismatch";
                      if (e.empty()) for (size_t si : segs_of[k]) seg[si].crc = crc_upd(0, d.data() + seg[si].off, (size_t)seg[si].len);
                  } catch (exception &x) { e = x.what(); }
                  g.lock(); if (!e.empty()) { if (err.empty()) err = e; } else { slot[k].d.swap(d); slot[k].ready = true; } cv.notify_all(); }
            }
            fclose(cf);
        };
        vector<thread> th; for (int i = 0; i < max(1, jobs); i++) th.emplace_back(worker);
        string werr;
        try {
            F fo(out, "wb"); setvbuf(fo.f, nullptr, _IOFBF, 1 << 22); F fr(in, "rb"); FSEEK(fr.f, (long long)rawpos, SEEK_SET); vector<uint8_t> rb(1 << 22);
            vector<uint64_t> left(nb); for (size_t k = 0; k < nb; k++) left[k] = B[k].ln;
            for (auto &sg : seg) {
                if (sg.blk < 0) { uint64_t L = sg.len; uint32_t c = 0; while (L) { size_t t = (size_t)min<uint64_t>(L, rb.size()); rd(fr.f, rb.data(), t); c = crc_upd(c, rb.data(), t); wr(fo.f, rb.data(), t); L -= t; } sg.crc = c; continue; }
                size_t k = (size_t)sg.blk;
                { unique_lock<mutex> g(mu); cv.wait(g, [&] { return slot[k].ready || !err.empty(); }); if (!slot[k].ready) throw runtime_error(err); }
                wr(fo.f, slot[k].d.data() + sg.off, (size_t)sg.len); left[k] -= sg.len;
                if (!left[k]) { lock_guard<mutex> g(mu); vector<uint8_t>().swap(slot[k].d); held--; cv.notify_all(); }
            }
        } catch (exception &x) { werr = x.what(); lock_guard<mutex> g(mu); if (err.empty()) err = werr; cv.notify_all(); }
        for (auto &t : th) t.join();
        if (!err.empty()) { error_code ec; fs::remove(out, ec); throw runtime_error(err); }
        uint32_t c2 = 0; for (auto &sg : seg) c2 = crc_combine(c2, sg.crc, sg.len);
        if (hascrc && c2 != crc) { error_code ec; fs::remove(out, ec); throw runtime_error("restored data failed the CRC-32 check"); }
        return;
    }
    TmpDir td(out); vector<pair<string, string>> pairs; vector<uint64_t> sizes;
    FSEEK(fi.f, (long long)p, SEEK_SET);
    for (size_t k = 0; k < B.size(); k++) { string f = td.at("b" + to_string(k)); { F fz(f + ".z", "wb"); copy_n(fi.f, fz.f, B[k].zl); } pairs.push_back({f + ".z", f}); sizes.push_back(B[k].ln); }
    run_all(decomp, pairs, sizes, jobs);
    for (size_t k = 0; k < B.size(); k++) if (fsize(pairs[k].second) != B[k].ln) throw runtime_error("block length mismatch");
    // rebuild: each set is its blocks in table order; raw comes from the container
    map<int, vector<size_t>> blist; for (size_t k = 0; k < B.size(); k++) blist[B[k].s].push_back(k);
    struct Rd { vector<string> files; size_t fi = 0; FILE *f = nullptr; };
    map<int, Rd> rs; for (auto &kv : blist) for (size_t k : kv.second) rs[kv.first].files.push_back(pairs[k].second);
    auto pull = [&](int s, uint8_t *dst, size_t L) {
        Rd &r = rs[s]; size_t got_ = 0;
        while (got_ < L) {
            if (!r.f) { if (r.fi >= r.files.size()) throw runtime_error("set data ran out"); r.f = fopen(r.files[r.fi].c_str(), "rb"); if (!r.f) throw runtime_error("cannot read block"); }
            size_t k = fread(dst + got_, 1, L - got_, r.f); got_ += k;
            if (got_ < L) { fclose(r.f); r.f = nullptr; r.fi++; }
        }
    };
    FSEEK(fi.f, (long long)rawpos, SEEK_SET);
    uint32_t c2 = 0; vector<uint8_t> b(W); i = 0;
    { F fo(out, "wb");
      for (auto &r : R) for (uint32_t c = 0; c < r.second; c++, i++) {
          size_t L = (size_t)min<uint64_t>(W, n - i * W);
          if (r.first == RAW) rd(fi.f, b.data(), L); else pull(r.first, b.data(), L);
          wr(fo.f, b.data(), L); c2 = crc_upd(c2, b.data(), L);
      }
      for (auto &kv : rs) if (kv.second.f) fclose(kv.second.f); }
    if (hascrc && c2 != crc) { error_code ec; fs::remove(out, ec); throw runtime_error("restored data failed the CRC-32 check"); }
}

int main(int argc, char **argv) {
    crc_init();
    if (argc < 4 || (strcmp(argv[1], "c") && strcmp(argv[1], "d"))) {
        fprintf(stderr, "Harrington Sets wrapper 1.4 (streaming, direct decode), Michael Hugh Harrington, MIT License\n"
                        "  hs_wrap c IN OUT --comp \"COMPRESSOR {in} {out}\" [--jobs N] [--block-mb B]\n"
                        "  hs_wrap d IN OUT --decomp \"DECOMPRESSOR {in} {out}\" [--jobs N]\n"
                        "  hs_wrap d IN OUT --decomp \"DECOMPRESSOR\" [--jobs N]   (no {in}/{out}: direct, stdin->stdout, no temp files; --hold N caps decoded blocks in memory)\n");
        return 2;
    }
    string comp, decomp; int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; long bmb = 16;
    for (int i = 4; i + 1 < argc; i += 2) {
        string k = argv[i];
        if (k == "--comp") comp = argv[i + 1]; else if (k == "--decomp") decomp = argv[i + 1];
        else if (k == "--jobs") jobs = atoi(argv[i + 1]); else if (k == "--hold") g_hold = atol(argv[i + 1]); else if (k == "--block-mb") bmb = atol(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    bool c = argv[1][0] == 'c';
    if (c ? comp.empty() : decomp.empty()) { fprintf(stderr, "error: --comp is needed to compress, --decomp to decompress\n"); return 2; }
    if (bmb < 1) bmb = 16;
    auto t = chrono::steady_clock::now();
    try { c ? compress(argv[2], argv[3], comp, jobs, bmb) : decompress(argv[2], argv[3], decomp, jobs); }
    catch (exception &e) { fprintf(stderr, "error: %s\n", e.what()); return 1; }
    printf("%s wall time %.1f s\n", c ? "compress" : "decompress", chrono::duration<double>(chrono::steady_clock::now() - t).count());
    return 0;
}

// MIT License
//
// Copyright (c) 2026 Michael Hugh Harrington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
