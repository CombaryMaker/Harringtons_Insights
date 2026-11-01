// hs_build.cpp (version 1.1) - Harrington Sets build scheduler for make-based projects: same compiler, same flags, same
// objects, same executable - only the ORDER in which the compile jobs are started changes.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// make -jN runs compiles in Makefile order and starts the next job whenever a core is free. Source files differ
// in cost by 100x or more, so when the heaviest files come late, one core is left grinding on them at the end
// while the others sit idle (the long tail). hs_build asks make for the list of object files it would build
// (make -n), sorts them heaviest first - by measured compile time from a previous build (profile) or by source
// size when there is none - and hands make that list as its goals: make still resolves every prerequisite
// (generated headers, generated sources), so the dependency order is respected, but the heavy files start first
// and the tail disappears. A second make then links everything exactly as before. Compile times are recorded
// through the hs_cc shim (CC="hs_cc gcc") into the profile for the next build.
//
// Usage: hs_build [--jobs N] [--make PATH] [--profile FILE] [--shim PATH] [--no-shim] [--pre TARGET] [--top F] [--dir D] -- MAKE_ARGS
//   e.g. hs_build --jobs 12 --profile .hs_build_profile -- all
//   --pre TARGET: a target to build first when the project generates headers/sources WITHOUT declaring them as
//   prerequisites of the objects (OpenSSL: --pre build_generated); CMake and most autotools Makefiles need none.
// Build: g++ -O2 -std=c++17 hs_build.cpp -o hs_build
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif
// Windows: cmd.exe has no /dev/null (it is NUL), and a command line that starts with a quote must be wrapped in one
// more pair of quotes or cmd strips the wrong ones (fixed in 1.1 after the first Windows run).
#ifdef _WIN32
static const char *NULL_OUT = " >NUL 2>&1", *NULL_ERR = " 2>NUL";
static string shell(const string &c) { return "\"" + c + "\""; }
#else
static const char *NULL_OUT = " >/dev/null 2>&1", *NULL_ERR = " 2>/dev/null";
static string shell(const string &c) { return c; }
#endif
static string q(const string &s) { return "\"" + s + "\""; }
static vector<string> tokens(const string &l) { vector<string> v; string cur; bool sq = false, dq = false;
    for (char c : l) { if (c == '\'' && !dq) { sq = !sq; continue; } if (c == '"' && !sq) { dq = !dq; continue; }
        if (c == ' ' && !sq && !dq) { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) v.push_back(cur); return v; }
static bool ends(const string &s, const char *e) { size_t n = strlen(e); return s.size() >= n && s.compare(s.size() - n, n, e) == 0; }

int main(int argc, char **argv) {
    int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; string mk = "make", profile = ".hs_build_profile", shim = "hs_cc", dir, pre, ccname = "gcc", cxxname = "g++"; bool useshim = true; double topfrac = 0.10; vector<string> margs;
    for (int i = 1; i < argc; i++) { string a = argv[i];
        if (a == "--") { for (int k = i + 1; k < argc; k++) margs.push_back(argv[k]); break; }
        else if (a == "--jobs" && i + 1 < argc) jobs = max(1, atoi(argv[++i])); else if (a == "--make" && i + 1 < argc) mk = argv[++i];
        else if (a == "--profile" && i + 1 < argc) profile = argv[++i]; else if (a == "--shim" && i + 1 < argc) shim = argv[++i];
        else if (a == "--no-shim") useshim = false; else if (a == "--pre" && i + 1 < argc) pre = argv[++i]; else if (a == "--top" && i + 1 < argc) topfrac = atof(argv[++i]); else if (a == "--cc" && i + 1 < argc) ccname = argv[++i]; else if (a == "--cxx" && i + 1 < argc) cxxname = argv[++i]; else if (a == "--dir" && i + 1 < argc) dir = argv[++i];
        else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; } }
    if (!dir.empty()) fs::current_path(dir);
    if (margs.empty()) {   // explicit goals replace make's default goal, so find it and add it to the targets
        FILE *p = POPEN(shell(q(mk) + " -pn" + NULL_ERR).c_str(), "r"); char b[4096];
        if (p) { while (fgets(b, sizeof b, p)) if (!strncmp(b, ".DEFAULT_GOAL := ", 17)) { string g = b + 17; while (!g.empty() && isspace((unsigned char)g.back())) g.pop_back(); margs.push_back(g); break; } PCLOSE(p); }
    }
    string ma; for (auto &a : margs) ma += " " + q(a);
    // the dry run must see the same CC/CXX as the real build: some projects put a hash of the compiler settings in
    // their object paths (zstd: obj/conf_<hash>/), so a dry run with another CC lists objects no rule builds (1.1)
    string cc = useshim ? " CC=" + q(shim + " " + ccname) + " CXX=" + q(shim + " " + cxxname) : "";
    auto t0 = chrono::steady_clock::now();
    // 1. what would make compile?  (dry run; compile = a command with -c and -o X.o)
    map<string, string> src_of; vector<string> order;   // object -> source, in Makefile order
    { FILE *p = POPEN(shell(q(mk) + " -n" + cc + ma + NULL_ERR).c_str(), "r"); if (!p) { fprintf(stderr, "error: cannot run make\n"); return 1; }
      char buf[1 << 16]; string line;
      while (fgets(buf, sizeof buf, p)) { line = buf; while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
          size_t sp = 0; while (sp < line.size() && line[sp] == ' ') sp++; line = line.substr(sp);
          for (string part : {line}) {   // (a line may chain commands with &&; the compile is the one with -c)
              auto tk = tokens(part); bool c = false; string obj, src;
              for (size_t i = 0; i < tk.size(); i++) { if (tk[i] == "-c") c = true; else if (tk[i] == "-o" && i + 1 < tk.size()) obj = tk[i + 1];
                  else if (ends(tk[i], ".c") || ends(tk[i], ".cc") || ends(tk[i], ".cpp") || ends(tk[i], ".cxx") || ends(tk[i], ".S") || ends(tk[i], ".s") || ends(tk[i], ".m")) src = tk[i]; }
              if (c && !obj.empty() && (ends(obj, ".o") || ends(obj, ".obj")) && !src_of.count(obj)) { src_of[obj] = src; order.push_back(obj); } } }
      PCLOSE(p); }
    if (order.empty()) { fprintf(stderr, "hs_build: make -n listed no compile jobs (already built?) - running plain make\n"); return system(shell(q(mk) + " -j" + to_string(jobs) + ma).c_str()); }
    // 2. costs: measured profile if present, else source size (generated sources not yet present get the median)
    map<string, double> prof; { ifstream f(profile); string l; while (getline(f, l)) { istringstream s(l); double st, du; string o; if (s >> st >> du >> o) prof[o] = max(prof[o], du); } }
    vector<double> sizes; map<string, double> cost; int measured = 0;
    for (auto &o : order) { error_code ec; auto sz = fs::file_size(src_of[o], ec); if (!ec) sizes.push_back((double)sz); }
    sort(sizes.begin(), sizes.end()); double med = sizes.empty() ? 1 : sizes[sizes.size() / 2];
    for (auto &o : order) { if (prof.count(o)) { cost[o] = prof[o] * 1e9; measured++; } else { error_code ec; auto sz = fs::file_size(src_of[o], ec); cost[o] = ec ? med : (double)sz; } }
    vector<string> goals = order; stable_sort(goals.begin(), goals.end(), [&](const string &a, const string &b) { return cost[a] > cost[b]; });
    fprintf(stderr, "hs_build: %zu compile jobs, %d with measured times (%s), %d jobs; heaviest first: %s\n", order.size(), measured, measured ? "profile" : "source-size estimate", jobs, goals[0].c_str());
    // 3. compile heaviest first (make resolves prerequisites), recording times through the shim
    // the shim is put in front of whatever compiler make uses (CC for C, CXX for C++); --cxx names the C++ compiler
    string env;
#ifdef _WIN32
    _putenv_s("HS_CC_LOG", profile.c_str());
#else
    setenv("HS_CC_LOG", profile.c_str(), 1);
#endif
    if (useshim) { ofstream(profile, ios::app); }
    // only the heavy head of the list is handed to make as explicit goals (make re-scans its goal list after every
    // finished job, so thousands of goals cost real CPU); the light files follow in Makefile order through the
    // user's own targets. Default: the heaviest 10% (at least 2 x jobs, at most 400).
    size_t top = (size_t)max<double>(2.0 * jobs, topfrac * goals.size()); top = min(top, min<size_t>(goals.size(), 400)); goals.resize(top);
    string g; for (auto &o : goals) g += " " + q(o);
    if (!pre.empty() && system(shell(q(mk) + " -j" + to_string(jobs) + cc + " " + pre + NULL_OUT).c_str())) { fprintf(stderr, "error: --pre target failed\n"); return 1; }
    // one make: the user's targets first (so linking, docs and other non-compile work overlap with compiling as in a
    // normal make): the heavy goals first, then the user's targets
    auto t1 = chrono::steady_clock::now();
    int rc = system(shell(q(mk) + " -j" + to_string(jobs) + cc + g + ma + NULL_OUT).c_str());
    auto t2 = chrono::steady_clock::now();
    if (rc) { fprintf(stderr, "error: make failed (exit %d)\n", rc); return 1; }
    fprintf(stderr, "hs_build: plan %.1f s, build %.1f s, total %.1f s\n", chrono::duration<double>(t1 - t0).count(), chrono::duration<double>(t2 - t1).count(), chrono::duration<double>(t2 - t0).count());
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
