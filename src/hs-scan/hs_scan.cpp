// hs_scan.cpp (safe smoothing fix 1 Nov 2026, Andrew Goodwin) - Harrington Sets scan wrapper: lets single-file scanners (YARA, ClamAV) use every core on
// ONE large file (memory dumps, disk images, packet captures, big logs).
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// How it works: the file is read in 64 KB windows and grouped into Harrington Sets by byte statistics
// (same labelling as hs_wrap: k-means into up to 3 sets plus a high-entropy set; nothing is skipped -
// every byte is scanned). Each contiguous run of one set is cut into pieces of at most B MB. Every piece is
// written out as a scan unit that also carries the next OV bytes of the ORIGINAL file, so any match up to
// OV+1 bytes long that starts in a piece is fully inside that unit; units are always contiguous ranges of the
// original file, so no false matches can form across joins. Units are grouped in folders by set (so each set
// can get its own rule file) and scanned in parallel. Hits are mapped back to original file offsets; YARA
// hits whose first matched string starts in an overlap zone are dropped (the next unit reports them).
//
// Usage:
//   hs_scan yara RULES INPUT [--yara PATH] [--rules-set N=FILE ...] [options]
//   hs_scan clam INPUT [--clamscan PATH] [--db PATH] [options]        (one clamscan per job, file lists)
//   hs_scan clamd INPUT [--clamdscan PATH] [options]                   (running clamd, --multiscan)
// options: --jobs N (default all cores)  --block-mb B (default 16)  --overlap-kb K (default 64)
//          --clam-args "..." extra clamscan/clamdscan options (e.g. --allmatch, --scan-pe=no)
//          --tmp DIR (default INPUT.hs_scan_tmp; needs about the input size in free space while running)
//
// Limits (same as any chunked scan): rules that look at the whole file (filesize, header/entry-point
// conditions, PE/ELF modules, archive or container unpacking in ClamAV) only see one unit at a time; the
// unit at offset 0 keeps true header offsets. Use it for raw-pattern scanning of big blobs; scan normal files
// normally. Matches longer than the overlap, or rules needing strings farther apart than the overlap, can be
// missed - raise --overlap-kb for those.
// Build: g++ -O2 -std=c++17 -pthread hs_scan.cpp -o hs_scan
//        zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_scan.cpp -o hs_scan.exe
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
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
#define FSEEK _fseeki64
#define POPEN _popen
#define PCLOSE _pclose
#else
#define FSEEK fseeko
#define POPEN popen
#define PCLOSE pclose
#endif
static const size_t W = 65536; static const int RAW = 255;

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
        while (i < nw) { size_t j = i; while (j < nw && lab[j] == lab[i]) j++;
            if (j - i < 8 && i > 0) { int nl = lab[i - 1];
                // safe smoothing (1 Nov 2026, from Andrew Goodwin's review of hs_zstd): a non-raw run is never relabelled RAW
                if (nl == RAW && lab[i] != RAW) nl = (j < nw && lab[j] != RAW) ? lab[j] : lab[i];
                for (size_t q = i; q < j; q++) lab[q] = nl; }
            i = j; }
    }
    return lab;
}
static string setname(int s) { return s == RAW ? "setR" : "set" + to_string(s); }
static string q(const string &s) { return "\"" + s + "\""; }
static vector<string> run_lines(const string &cmd) {   // runs a command, returns its stdout lines
#ifdef _WIN32
    string c = "\"" + cmd + " 2>NUL\"";
#else
    string c = cmd + " 2>/dev/null";
#endif
    vector<string> out; FILE *p = POPEN(c.c_str(), "r"); if (!p) throw runtime_error("cannot run: " + cmd);
    char buf[65536]; string cur;
    while (fgets(buf, sizeof buf, p)) { cur += buf; if (!cur.empty() && cur.back() == '\n') { while (!cur.empty() && (cur.back() == '\n' || cur.back() == '\r')) cur.pop_back(); out.push_back(cur); cur.clear(); } }
    if (!cur.empty()) out.push_back(cur);
    PCLOSE(p); return out;
}
struct Unit { int s; uint64_t a, b, end; string path; };   // piece [a,b), unit file holds [a,end)

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Harrington Sets scan wrapper (YARA / ClamAV), Michael Hugh Harrington, MIT License\n"
                        "  hs_scan yara RULES INPUT [--yara PATH] [--rules-set N=FILE] [options]\n"
                        "  hs_scan clam INPUT [--clamscan PATH] [--db PATH] [options]\n"
                        "  hs_scan clamd INPUT [--clamdscan PATH] [options]\n"
                        "  options: --jobs N  --block-mb B (16)  --overlap-kb K (64)  --tmp DIR\n");
        return 2;
    }
    string mode = argv[1]; int ai = 2; string rules;
    if (mode == "yara") { if (argc < 4) return 2; rules = argv[ai++]; }
    else if (mode != "clam" && mode != "clamd") { fprintf(stderr, "unknown mode %s\n", mode.c_str()); return 2; }
    string in = argv[ai++];
    int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2;
    long bmb = 16, ovkb = 64; string clamargs; string tmp = in + ".hs_scan_tmp", yexe = "yara", cexe = "clamscan", cdexe = "clamdscan", db;
    map<int, string> setrules;
    for (int i = ai; i + 1 < argc; i += 2) {
        string k = argv[i], v = argv[i + 1];
        if (k == "--jobs") jobs = max(1, atoi(v.c_str())); else if (k == "--block-mb") bmb = max(1L, atol(v.c_str()));
        else if (k == "--overlap-kb") ovkb = max(1L, atol(v.c_str())); else if (k == "--tmp") tmp = v;
        else if (k == "--yara") yexe = v; else if (k == "--clamscan") cexe = v; else if (k == "--clamdscan") cdexe = v; else if (k == "--db") db = v; else if (k == "--clam-args") clamargs = " " + v;
        else if (k == "--rules-set") { auto e = v.find('='); if (e == string::npos) return 2; string n = v.substr(0, e); setrules[n == "R" ? RAW : atoi(n.c_str())] = v.substr(e + 1); }
        else { fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    auto t0 = chrono::steady_clock::now();
    try {
        // pass 1: window labels
        vector<array<double, 7>> Fv; vector<double> E; vector<size_t> Ls; uint64_t n = 0;
        { FILE *f = fopen(in.c_str(), "rb"); if (!f) throw runtime_error("cannot open " + in); vector<uint8_t> b(W); size_t k;
          while ((k = fread(b.data(), 1, W, f)) > 0) { array<double, 7> ft; double e; features(b.data(), k, ft, e); Fv.push_back(ft); E.push_back(e); Ls.push_back(k); n += k; if (k < W) break; }
          fclose(f); }
        auto lab = labels(Fv, E, Ls); size_t nw = lab.size();
        // units: runs of one set, cut into pieces of at most B MB, each extended by the overlap
        uint64_t B = (uint64_t)bmb << 20, OV = (uint64_t)ovkb << 10; vector<Unit> U;
        for (size_t i = 0; i < nw;) {
            size_t j = i; while (j < nw && lab[j] == lab[i]) j++;
            uint64_t a = (uint64_t)i * W, e = min<uint64_t>(n, (uint64_t)j * W), np = max<uint64_t>(1, (e - a + B - 1) / B), per = (e - a + np - 1) / np;
            for (uint64_t k = 0; k < np; k++) { uint64_t pa = a + k * per, pb = min(e, pa + per); if (pa < pb) U.push_back({lab[i], pa, pb, min(n, pb + OV), ""}); }
            i = j;
        }
        error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp);
        { FILE *f = fopen(in.c_str(), "rb"); vector<uint8_t> b(1 << 20);
          for (size_t u = 0; u < U.size(); u++) {
              fs::create_directories(fs::path(tmp) / setname(U[u].s));
              char nm[32]; snprintf(nm, sizeof nm, "u%06zu", u); U[u].path = (fs::path(tmp) / setname(U[u].s) / nm).string();
              FILE *o = fopen(U[u].path.c_str(), "wb"); if (!o) throw runtime_error("cannot write temporary unit");
              FSEEK(f, (long long)U[u].a, SEEK_SET); uint64_t left = U[u].end - U[u].a;
              while (left) { size_t k = (size_t)min<uint64_t>(left, b.size()); if (fread(b.data(), 1, k, f) != k) throw runtime_error("read error"); fwrite(b.data(), 1, k, o); left -= k; }
              fclose(o);
          }
          fclose(f); }
        map<string, size_t> byname; for (size_t u = 0; u < U.size(); u++) byname[fs::path(U[u].path).filename().string()] = u;
        auto unit_of = [&](const string &line) -> long { size_t p = line.find("u0"); while (p != string::npos) { string nm = line.substr(p, 7); if (byname.count(nm)) return (long)byname[nm]; p = line.find("u0", p + 1); } return -1; };
        double tsplit = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        set<int> sets; for (auto &u : U) sets.insert(u.s);
        printf("Harrington Sets scan: %llu bytes, %zu units in sets", (unsigned long long)n, U.size()); for (int s : sets) printf(" %s", setname(s).c_str());
        printf(", %d jobs, %ld MB pieces, %ld KB overlap (split %.1f s)\n", jobs, bmb, ovkb, tsplit);
        // scan
        vector<string> lines;
        if (mode == "yara") {
            // one YARA run per distinct rule file (rules are compiled once per run); a .yarc file is used precompiled
            auto ycmd = [&](const string &r, const string &target, bool rec) {
                bool pre = r.size() > 5 && r.substr(r.size() - 5) == ".yarc";
                return q(yexe) + " -s -w -p " + to_string(jobs) + (rec ? " -r" : "") + (pre ? " -C" : "") + " " + q(r) + " " + q(target);
            };
            bool shared = true; for (int s : sets) if (setrules.count(s)) shared = false;
            if (shared) { auto L = run_lines(ycmd(rules, tmp, true)); lines.insert(lines.end(), L.begin(), L.end()); }
            else for (int s : sets) {
                string r = setrules.count(s) ? setrules[s] : rules;
                auto L = run_lines(ycmd(r, (fs::path(tmp) / setname(s)).string(), false)); lines.insert(lines.end(), L.begin(), L.end());
            }
            // parse: "Rule path" then "0xOFF:$id: data" lines; keep a rule hit when its first string starts in the piece
            map<string, set<uint64_t>> hits; map<string, set<string>> nostr; string currule; long cu = -1; vector<pair<uint64_t, string>> pend; bool any = false;
            auto flush = [&]() {
                if (cu < 0 || currule.empty()) return; const Unit &u = U[cu];
                if (!any) { char rg[64]; snprintf(rg, sizeof rg, "%llu-%llu", (unsigned long long)u.a, (unsigned long long)u.b); nostr[currule].insert(rg); return; }
                uint64_t first = UINT64_MAX; for (auto &p : pend) first = min(first, p.first);
                if (first >= u.b) return;   // starts in the overlap: the next unit reports it
                for (auto &p : pend) if (p.first < u.b) hits[currule + "  " + p.second].insert(p.first);
            };
            for (auto &l : lines) {
                if (l.rfind("0x", 0) == 0) { size_t c = l.find(':'); size_t c2 = l.find(':', c + 1); uint64_t off = strtoull(l.substr(2, c - 2).c_str(), nullptr, 16);
                    if (cu >= 0) { pend.push_back({U[cu].a + off, l.substr(c + 1, c2 - c - 1)}); any = true; } }
                else { flush(); size_t sp = l.find(' '); currule = sp == string::npos ? l : l.substr(0, sp); cu = unit_of(l); pend.clear(); any = false; }
            }
            flush();
            set<string> rset; for (auto &h : hits) rset.insert(h.first.substr(0, h.first.find("  "))); for (auto &h : nostr) rset.insert(h.first);
            printf("YARA rules matched: %zu\n", rset.size());
            bool all = getenv("HS_SCAN_ALL") != nullptr;
            for (auto &h : hits) { printf("  %s at", h.first.c_str()); int c = 0; for (auto o : h.second) { if (c++ == 20 && !all) { printf(" ... (%zu total)", h.second.size()); break; } printf(" 0x%llx", (unsigned long long)o); } printf("\n"); }
            for (auto &h : nostr) { printf("  %s (no strings; in byte ranges", h.first.c_str()); for (auto &r : h.second) printf(" %s", r.c_str()); printf(")\n"); }
        } else {
            if (mode == "clam") {
                vector<vector<string>> lists(jobs); size_t k = 0; vector<size_t> ord(U.size()); for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
                sort(ord.begin(), ord.end(), [&](size_t x, size_t y) { return U[x].end - U[x].a > U[y].end - U[y].a; });
                for (size_t u : ord) lists[k++ % jobs].push_back(U[u].path);
                mutex mu; vector<thread> th;
                for (int j = 0; j < jobs; j++) {
                    if (lists[j].empty()) continue;
                    string lf = (fs::path(tmp) / ("list" + to_string(j) + ".txt")).string(); { ofstream o(lf); for (auto &p : lists[j]) o << p << "\n"; }
                    th.emplace_back([&, lf] { auto L = run_lines(q(cexe) + " --no-summary --infected" + clamargs + (db.empty() ? string() : " -d " + q(db)) + " --file-list=" + q(lf)); lock_guard<mutex> g(mu); lines.insert(lines.end(), L.begin(), L.end()); });
                }
                for (auto &t : th) t.join();
            } else {
                auto L = run_lines(q(cdexe) + " --multiscan --fdpass --no-summary --infected" + clamargs + " " + q(tmp)); lines = L;
            }
            map<string, set<string>> found;   // signature -> byte ranges of the pieces it was found in
            for (auto &l : lines) {
                size_t f = l.rfind(" FOUND"); if (f == string::npos) continue; size_t c = l.rfind(": ", f); if (c == string::npos) continue;
                long u = unit_of(l.substr(0, c)); if (u < 0) continue; string sig = l.substr(c + 2, f - c - 2);
                char rg[64]; snprintf(rg, sizeof rg, "%llu-%llu", (unsigned long long)U[u].a, (unsigned long long)U[u].end); found[sig].insert(rg);
            }
            printf("ClamAV signatures found: %zu\n", found.size());
            for (auto &h : found) { printf("  %s  in byte ranges", h.first.c_str()); int c = 0; for (auto &r : h.second) { if (c++ == 20) { printf(" ..."); break; } printf(" %s", r.c_str()); } printf("\n"); }
        }
        fs::remove_all(tmp, ec);
        printf("scan wall time %.1f s\n", chrono::duration<double>(chrono::steady_clock::now() - t0).count());
    } catch (exception &e) { error_code ec; fs::remove_all(tmp, ec); fprintf(stderr, "error: %s\n", e.what()); return 1; }
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
