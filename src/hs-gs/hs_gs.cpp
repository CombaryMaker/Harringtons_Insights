// hs_gs.cpp - Harrington Sets wrapper for Ghostscript: renders ONE large PDF on every core.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Ghostscript renders a document page by page on one core (its NumRenderingThreads option only splits a single
// page into bands). hs_gs runs several Ghostscript processes on the same PDF, each rendering contiguous page
// sets (contiguous pages share fonts and images, so each process keeps its caches warm). Page sets are handed
// out from a shared queue and get smaller toward the end of the document (guided scheduling), so a run of
// expensive image pages cannot leave one process working long after the others finish - the weakness of the
// usual "split the page range into N equal parts" trick. Each page image is renamed to its true page number,
// so the output files are the same as a single Ghostscript run (checked byte for byte with --check).
//
// Usage: hs_gs INPUT.pdf --out PATTERN [--jobs N] [--gs PATH] [--split equal|guided] [--check] -- GS_DEVICE_ARGS
//   PATTERN is the output file pattern with one %d-style field for the page number, e.g. out/page_%04d.png
//   GS_DEVICE_ARGS are the normal Ghostscript options, e.g. -sDEVICE=png16m -r150
//   --split equal renders N equal page ranges (the common trick) for comparison; default guided
//   --check also runs one plain Ghostscript process and compares every output file byte for byte
// Works with page-per-file raster devices (png*, jpeg, tiff*, pnm, bmp ...). Not for devices that write one
// combined file (pdfwrite, multi-page tiff) - those would need merging.
// Build: g++ -O2 -std=c++17 -pthread hs_gs.cpp -o hs_gs
//        zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_gs.cpp -o hs_gs.exe
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iterator>
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
static const char *NUL_ = " 2>NUL";
#else
#define POPEN popen
#define PCLOSE pclose
static const char *NUL_ = " 2>/dev/null";
#endif
static string q(const string &s) { return "\"" + s + "\""; }
static int sh(const string &c) {
#ifdef _WIN32
    return system(("\"" + c + "\"").c_str());
#else
    return system(c.c_str());
#endif
}
static string capture(const string &c) {
#ifdef _WIN32
    string cc = "\"" + c + NUL_ + "\"";
#else
    string cc = c + NUL_;
#endif
    FILE *p = POPEN(cc.c_str(), "r"); if (!p) return ""; string s; char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, p)) > 0) s.append(b, k); PCLOSE(p); return s;
}
static string fmt_pattern(const string &pat, long n) { char b[4096]; snprintf(b, sizeof b, pat.c_str(), n); return b; }
static bool same_file(const string &a, const string &b) {
    ifstream x(a, ios::binary), y(b, ios::binary); if (!x || !y) return false;
    return vector<char>(istreambuf_iterator<char>(x), {}) == vector<char>(istreambuf_iterator<char>(y), {});
}

int main(int argc, char **argv) {
    string in, out, gs =
#ifdef _WIN32
        "gswin64c";
#else
        "gs";
#endif
    string split = "guided"; int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; bool check = false; vector<string> gargs;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--") { for (int k = i + 1; k < argc; k++) gargs.push_back(argv[k]); break; }
        else if (a == "--out" && i + 1 < argc) out = argv[++i]; else if (a == "--jobs" && i + 1 < argc) jobs = max(1, atoi(argv[++i]));
        else if (a == "--gs" && i + 1 < argc) gs = argv[++i]; else if (a == "--split" && i + 1 < argc) split = argv[++i];
        else if (a == "--check") check = true; else if (in.empty()) in = a; else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (in.empty() || out.empty() || gargs.empty() || out.find('%') == string::npos) {
        fprintf(stderr, "Harrington Sets wrapper for Ghostscript, Michael Hugh Harrington, MIT License\n"
                        "  hs_gs INPUT.pdf --out DIR/page_%%04d.png [--jobs N] [--gs PATH] [--split equal|guided] [--check] -- -sDEVICE=png16m -r150\n");
        return 2;
    }
    string ga; for (auto &x : gargs) ga += " " + q(x);
    // page count (Ghostscript itself, no other tools needed)
    string pin = in; for (auto &c : pin) if (c == '\\') c = '/';
    string pc = capture(q(gs) + " -q -dNODISPLAY -dNOSAFER -c " + q("(" + pin + ") (r) file runpdfbegin pdfpagecount = quit"));
    long pages = atol(pc.c_str()); if (pages <= 0) { fprintf(stderr, "error: could not read the page count of %s\n", in.c_str()); return 1; }
    jobs = (int)min<long>(jobs, pages);
    // page sets: equal ranges, or guided (each set = remaining pages / (2 x jobs), at least 1)
    vector<pair<long, long>> sets;
    if (split == "equal") { long per = (pages + jobs - 1) / jobs; for (long a = 1; a <= pages; a += per) sets.push_back({a, min(pages, a + per - 1)}); }
    else { long a = 1; while (a <= pages) { long left = pages - a + 1, sz = max(1L, left / (2L * jobs)); sets.push_back({a, a + sz - 1}); a += sz; } }
    fs::path outdir = fs::path(fmt_pattern(out, 1)).parent_path(); if (!outdir.empty()) fs::create_directories(outdir);
    string tmp = (outdir.empty() ? fs::path(".") : outdir).string() + "/.hs_gs_tmp"; error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp);
    auto t0 = chrono::steady_clock::now();
    atomic<size_t> next{0}; mutex mu; string err; vector<double> busy(jobs, 0);
    auto worker = [&](int w) {
        for (size_t k; (k = next++) < sets.size();) {
            long a = sets[k].first, b = sets[k].second; auto s0 = chrono::steady_clock::now();
            string pat = tmp + "/s" + to_string(k) + "_%06d";
            int r = sh(q(gs) + " -q -dBATCH -dNOPAUSE -dSAFER -dFirstPage=" + to_string(a) + " -dLastPage=" + to_string(b) + ga + " -sOutputFile=" + q(pat) + " " + q(in) + NUL_);
            if (r) { lock_guard<mutex> g(mu); if (err.empty()) err = "Ghostscript failed on pages " + to_string(a) + "-" + to_string(b); continue; }
            for (long p = a; p <= b; p++) { string src = fmt_pattern(pat, p - a + 1), dst = fmt_pattern(out, p); error_code e2; fs::rename(src, dst, e2); if (e2) { lock_guard<mutex> g(mu); if (err.empty()) err = "missing page image " + to_string(p); } }
            busy[w] += chrono::duration<double>(chrono::steady_clock::now() - s0).count();
        }
    };
    vector<thread> th; for (int w = 0; w < jobs; w++) th.emplace_back(worker, w); for (auto &t : th) t.join();
    double tw = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    fs::remove_all(tmp, ec);
    if (!err.empty()) { fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    double bmax = *max_element(busy.begin(), busy.end()), bmin = *min_element(busy.begin(), busy.end());
    fprintf(stderr, "hs_gs: %ld pages, %zu page sets (%s), %d jobs; wall %.1f s; busiest process %.1f s, least busy %.1f s\n", pages, sets.size(), split.c_str(), jobs, tw, bmax, bmin);
    if (check) {
        string cdir = tmp + "_check"; fs::remove_all(cdir, ec); fs::create_directories(cdir); auto c0 = chrono::steady_clock::now();
        string pat = cdir + "/p_%06d";
        sh(q(gs) + " -q -dBATCH -dNOPAUSE -dSAFER" + ga + " -sOutputFile=" + q(pat) + " " + q(in) + NUL_);
        double tp = chrono::duration<double>(chrono::steady_clock::now() - c0).count(); long diff = 0;
        for (long p = 1; p <= pages; p++) if (!same_file(fmt_pattern(pat, p), fmt_pattern(out, p))) { if (diff < 5) fprintf(stderr, "  page %ld differs\n", p); diff++; }
        fs::remove_all(cdir, ec);
        fprintf(stderr, "check: plain Ghostscript %.1f s; %ld pages compared, %ld differ (%s); speed-up %.2fx\n", tp, pages, diff, diff ? "NOT identical" : "IDENTICAL", tp / tw);
    }
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
