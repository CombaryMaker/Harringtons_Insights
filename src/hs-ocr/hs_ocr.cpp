// hs_ocr.cpp - Harrington Sets wrapper for Tesseract OCR: OCRs ONE multi-page document on every core.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Tesseract OCRs the pages of a document one after another (its OpenMP threads only help inside one page).
// hs_ocr takes the document as a list of page images (the list file Tesseract itself accepts, one image path per
// line, or a folder of images in name order), cuts it into contiguous page sets handed out from a shared queue and
// shrinking toward the end (guided scheduling, as in hs_gs and HsTika), runs one single-threaded Tesseract process
// per core, and joins the text in page order - the same text a single Tesseract run on the whole list writes
// (checked byte for byte with --check). Dense pages cost many times more than sparse ones, so balancing matters.
//
// Usage: hs_ocr LIST.txt|FOLDER OUTBASE [--jobs N] [--tesseract PATH] [--split equal|guided] [--check] [-- TESSERACT_OPTIONS]
//   writes OUTBASE.txt (text output). TESSERACT_OPTIONS e.g. -l eng --psm 3
// Build: g++ -O2 -std=c++17 -pthread hs_ocr.cpp -o hs_ocr
//        zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_ocr.cpp -o hs_ocr.exe
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
#include <filesystem>
#include <fstream>
#include <iterator>
namespace fs = std::filesystem;
using namespace std;
static string q(const string &s) { return "\"" + s + "\""; }
static int sh(const string &c) {
#ifdef _WIN32
    return system(("\"" + c + " >NUL 2>NUL\"").c_str());
#else
    return system((c + " >/dev/null 2>&1").c_str());
#endif
}
static string slurp(const string &p) { ifstream f(p, ios::binary); return string(istreambuf_iterator<char>(f), {}); }

int main(int argc, char **argv) {
    string in, outb, tess = "tesseract", split = "guided"; int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; bool check = false; string topts;
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--") { for (int k = i + 1; k < argc; k++) topts += " " + q(argv[k]); break; }
        else if (a == "--jobs" && i + 1 < argc) jobs = max(1, atoi(argv[++i])); else if (a == "--tesseract" && i + 1 < argc) tess = argv[++i];
        else if (a == "--split" && i + 1 < argc) split = argv[++i]; else if (a == "--check") check = true;
        else if (pos == 0) { in = a; pos++; } else if (pos == 1) { outb = a; pos++; } else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (in.empty() || outb.empty()) {
        fprintf(stderr, "Harrington Sets wrapper for Tesseract, Michael Hugh Harrington, MIT License\n"
                        "  hs_ocr LIST.txt|FOLDER OUTBASE [--jobs N] [--tesseract PATH] [--split equal|guided] [--check] [-- -l eng]\n");
        return 2;
    }
    vector<string> pages;
    if (fs::is_directory(in)) { for (auto &e : fs::directory_iterator(in)) if (e.is_regular_file()) pages.push_back(e.path().string()); sort(pages.begin(), pages.end()); }
    else { ifstream f(in); string l; while (getline(f, l)) { while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back(); if (!l.empty()) pages.push_back(l); } }
    long n = (long)pages.size(); if (n == 0) { fprintf(stderr, "error: no pages\n"); return 1; }
    jobs = (int)min<long>(jobs, n);
    vector<pair<long, long>> sets;   // [first, last] page indices, 0-based
    if (split == "equal") { long per = (n + jobs - 1) / jobs; for (long a = 0; a < n; a += per) sets.push_back({a, min(n, a + per) - 1}); }
    else { long a = 0; while (a < n) { long sz = max(1L, (n - a) / (2L * jobs)); sets.push_back({a, a + sz - 1}); a += sz; } }
    string tmp = outb + ".hs_ocr_tmp"; error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp);
#ifdef _WIN32
    _putenv("OMP_THREAD_LIMIT=1");   // one core per Tesseract process; the cores are used by running several at once
#else
    setenv("OMP_THREAD_LIMIT", "1", 1);
#endif
    auto t0 = chrono::steady_clock::now();
    atomic<size_t> next{0}; mutex mu; string err; vector<double> busy(jobs, 0);
    auto worker = [&](int w) {
        for (size_t k; (k = next++) < sets.size();) {
            auto s0 = chrono::steady_clock::now(); string lst = tmp + "/set" + to_string(k) + ".list", ob = tmp + "/set" + to_string(k);
            { ofstream o(lst, ios::binary); for (long p = sets[k].first; p <= sets[k].second; p++) o << pages[p] << "\n"; }
            if (sh(q(tess) + " " + q(lst) + " " + q(ob) + topts) != 0 || !fs::exists(ob + ".txt")) { lock_guard<mutex> g(mu); if (err.empty()) err = "Tesseract failed on pages " + to_string(sets[k].first + 1) + "-" + to_string(sets[k].second + 1); }
            busy[w] += chrono::duration<double>(chrono::steady_clock::now() - s0).count();
        }
    };
    vector<thread> th; for (int w = 0; w < jobs; w++) th.emplace_back(worker, w); for (auto &t : th) t.join();
    if (!err.empty()) { fs::remove_all(tmp, ec); fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    {   // Tesseract writes its page separator (form feed) BETWEEN pages, so one goes between consecutive page sets
        ofstream o(outb + ".txt", ios::binary);
        for (size_t k = 0; k < sets.size(); k++) { if (k) o << '\f'; o << slurp(tmp + "/set" + to_string(k) + ".txt"); } }
    double tw = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    fs::remove_all(tmp, ec);
    fprintf(stderr, "hs_ocr: %ld pages, %zu page sets (%s), %d jobs; wall %.1f s; busiest process %.1f s, least busy %.1f s\n", n, sets.size(), split.c_str(), jobs,
            tw, *max_element(busy.begin(), busy.end()), *min_element(busy.begin(), busy.end()));
    if (check) {   // plain single Tesseract run on the whole list, with Tesseract's normal threading
#ifdef _WIN32
        _putenv("OMP_THREAD_LIMIT=");
#else
        unsetenv("OMP_THREAD_LIMIT");
#endif
        string lst = outb + ".check_list.txt", ob = outb + ".check"; { ofstream o(lst, ios::binary); for (auto &p : pages) o << p << "\n"; }
        auto c0 = chrono::steady_clock::now(); sh(q(tess) + " " + q(lst) + " " + q(ob) + topts);
        double tp = chrono::duration<double>(chrono::steady_clock::now() - c0).count();
        bool same = slurp(ob + ".txt") == slurp(outb + ".txt");
        fprintf(stderr, "check: plain Tesseract %.1f s; text %s; speed-up %.2fx\n", tp, same ? "IDENTICAL" : "DIFFERENT", tp / tw);
        fs::remove(lst, ec); fs::remove(ob + ".txt", ec);
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
