// px_test.cpp - runs the approved paq8px / Harrington Sets wrapper test matrix (8 tests x 10 files = 80 cycles)
// in its own folder, times every compression and decompression (wall clock), checks every round trip byte for
// byte, deletes each test's outputs after measuring, and appends one line per cycle to results.txt.
// Michael Hugh Harrington, 2026. MIT License.
// Build: zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static px_test.cpp -o px_test.exe
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iterator>
namespace fs = std::filesystem;
using namespace std;
#ifdef _WIN32
static const string PX = "paq8px.exe", HW = "hs_wrap.exe";
static string q(const string &s) { return "\"" + s + "\""; }
static int sh(const string &c) { return system(("\"" + c + " >NUL 2>NUL\"").c_str()); }
#else
static const string PX = "./paq8px", HW = "./hs_wrap";
static string q(const string &s) { return "'" + s + "'"; }
static int sh(const string &c) { return system((c + " >/dev/null 2>&1").c_str()); }
#endif
static double now() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }
static bool same(const string &a, const string &b) {
    ifstream x(a, ios::binary), y(b, ios::binary); if (!x || !y) return false;
    return vector<char>(istreambuf_iterator<char>(x), {}) == vector<char>(istreambuf_iterator<char>(y), {});
}
struct T { string id, desc; int level; int block; int jobs; };   // block 0 = plain paq8px single stream
int main(int argc, char **argv) {
    int allbad = 0;
    vector<T> tests = {
        {"T1", "paq8px -1 single stream (baseline)", 1, 0, 1},
        {"T2", "wrapper, paq8px -1, 16 MB blocks (sets only), 12 jobs", 1, 16, 12},
        {"T3", "wrapper, paq8px -1, 2 MB blocks, 12 jobs", 1, 2, 12},
        {"T4", "wrapper, paq8px -1, 1 MB blocks, 12 jobs", 1, 1, 12},
        {"T5", "wrapper, paq8px -1, 1 MB blocks, 6 jobs", 1, 1, 6},
        {"T6", "wrapper, paq8px -1, 1 MB blocks, 3 jobs", 1, 1, 3},
        {"T7", "wrapper, paq8px -1, 1 MB blocks, 1 job (cost of splitting alone)", 1, 1, 1},
        {"T8", "wrapper, paq8px -6, 1 MB blocks, 6 jobs", 6, 1, 6}};
    vector<string> files = {"dickens", "mozilla", "mr", "nci", "osdb", "reymont", "samba", "sao", "webster", "x-ray"};
    string only = argc > 1 ? argv[1] : "";   // optional: run a single test id
    FILE *log = fopen("results.txt", "a"); if (!log) return 1;
    fprintf(log, "\npaq8px wrapper test matrix - test files are the first 3 MB (3,145,728 bytes) of 10 Silesia files\n");
    fprintf(log, "test | file | original bytes | compressed bytes | 1,000,000 bits down to | ratio | compress s | decompress s | exact\n");
    fflush(log);
    fs::create_directories("work");
    for (auto &t : tests) {
        if (!only.empty() && only != t.id) continue;
        fprintf(log, "--- %s: %s\n", t.id.c_str(), t.desc.c_str()); fflush(log);
        double sc = 0, sd = 0; unsigned long long so = 0, sz = 0; int bad = 0;
        for (auto &f : files) {
            string in = f + ".3mb", z = "work/" + f + ".z", d = "work/" + f + ".out";
            fs::remove(z); fs::remove(d);
            string L = "-" + to_string(t.level);
            string cc, dc;
            if (t.block == 0) { cc = PX + " " + L + " " + q(in) + " " + q(z); dc = PX + " -d " + q(z) + " " + q(d); }
            else {
                cc = HW + " c " + q(in) + " " + q(z) + " --comp \\\"" + PX + " " + L + " {in} {out}\\\" --jobs " + to_string(t.jobs) + " --block-mb " + to_string(t.block);
                dc = HW + " d " + q(z) + " " + q(d) + " --decomp \\\"" + PX + " -d {in} {out}\\\" --jobs " + to_string(t.jobs);
            }
#ifndef _WIN32
            for (size_t p; (p = cc.find("\\\"")) != string::npos;) cc.replace(p, 2, "\"");
            for (size_t p; (p = dc.find("\\\"")) != string::npos;) dc.replace(p, 2, "\"");
#else
            for (size_t p; (p = cc.find("\\\"")) != string::npos;) cc.replace(p, 2, "\"");
            for (size_t p; (p = dc.find("\\\"")) != string::npos;) dc.replace(p, 2, "\"");
#endif
            double a = now(); int r1 = sh(cc); double ct = now() - a;
            unsigned long long zs = fs::exists(z) ? fs::file_size(z) : 0;
            a = now(); int r2 = r1 ? 1 : sh(dc); double dt = now() - a;
            bool ok = !r1 && !r2 && same(in, d);
            unsigned long long os_ = fs::exists(in) ? fs::file_size(in) : 0;
            fprintf(log, "%s | %s | %llu | %llu | %.0f | %.3f | %.1f | %.1f | %s\n", t.id.c_str(), f.c_str(), os_, zs,
                    os_ ? 1e6 * zs / os_ : 0.0, zs ? (double)os_ / zs : 0.0, ct, dt, ok ? "yes" : (r1 ? "COMPRESS FAILED" : r2 ? "DECOMPRESS FAILED" : "MISMATCH"));
            fflush(log);
            sc += ct; sd += dt; so += os_; sz += zs; if (!ok) { bad++; allbad++; }
            fs::remove(z); fs::remove(d);
        }
        fprintf(log, "%s TOTAL | %llu | %llu | %.0f | compress %.1f s | decompress %.1f s | failures %d\n", t.id.c_str(), so, sz, so ? 1e6 * sz / so : 0.0, sc, sd, bad);
        fflush(log);
    }
    fprintf(log, "ALL DONE\n"); fclose(log);
    return allbad ? 1 : 0;   // nonzero exit when any cycle failed
}
