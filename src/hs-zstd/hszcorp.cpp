// hszcorp.cpp - (A) corpus run: the 12 standard Silesia files one by one, LZ4 1.10.0 default vs zstd 1.5.6 default vs
// hs_zstd 1.1 (12 jobs) at 16 MB and 4 MB blocks; 3 decode runs per file per tool (median), every decode CRC-checked;
// decode multiple M = summed per-file median decode time / LZ4's (Compression Tier Standard).
// (B) memory: 2 GB Silesia repeated, decode time and peak memory for zstd, hs_zstd 1.0 and hs_zstd 1.1 at several
// block sizes and core counts, 2 runs each. Big files deleted at the end.
// Michael Hugh Harrington, 2026. MIT License.
// Build: zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hszcorp.cpp -o hszcorp.exe
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
namespace fs = std::filesystem;
using namespace std;
static const uint64_t TARGET = 10000000000ULL;
static FILE *LOG;
static void logf(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(LOG, fmt, a); va_end(a); fflush(LOG); }

static uint32_t T8[8][256];
static void crc_init() {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; T8[0][i] = c; }
    for (int t = 1; t < 8; t++) for (int i = 0; i < 256; i++) T8[t][i] = (T8[t - 1][i] >> 8) ^ T8[0][T8[t - 1][i] & 255];
}
static uint32_t crc_upd(uint32_t c, const uint8_t *p, size_t n) {
    c = ~c;
    while (n >= 8) {
        uint32_t a = c ^ (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24), b = p[4] | p[5] << 8 | p[6] << 16 | (uint32_t)p[7] << 24;
        c = T8[7][a & 255] ^ T8[6][(a >> 8) & 255] ^ T8[5][(a >> 16) & 255] ^ T8[4][a >> 24] ^ T8[3][b & 255] ^ T8[2][(b >> 8) & 255] ^ T8[1][(b >> 16) & 255] ^ T8[0][b >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = T8[0][(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
static bool file_crc(const string &p, uint64_t &len, uint32_t &crc) {
    FILE *f = fopen(p.c_str(), "rb"); if (!f) return false; vector<uint8_t> b(1 << 22); size_t k; len = 0; crc = 0;
    while ((k = fread(b.data(), 1, b.size(), f)) > 0) { crc = crc_upd(crc, b.data(), k); len += k; }
    fclose(f); return true;
}
struct Res { int code; double sec; double peakMB; };
static Res run(const string &cmd) {
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{}; li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    STARTUPINFOA si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    string full = "cmd.exe /c \"" + cmd + "\"";
    vector<char> buf(full.begin(), full.end()); buf.push_back(0);
    auto t0 = chrono::steady_clock::now();
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(job); return {-1, 0, 0}; }
    AssignProcessToJobObject(job, pi.hProcess); ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    double sec = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION q{}; QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &q, sizeof q, nullptr);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job);
    return {(int)code, sec, q.PeakJobMemoryUsed / 1048576.0};
}
static string fmt(uint64_t v) { string s = to_string(v); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ","); return s; }
static double median(vector<double> v) { sort(v.begin(), v.end()); size_t n = v.size(); return n ? (n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2) : 0; }


static vector<string> PARTS = {"dickens", "mozilla", "mr", "nci", "ooffice", "osdb", "reymont", "samba", "sao", "webster", "x-ray", "xml"};
static string findp(const string &n) { for (string d : {string("..\\v36-cores-silesia-2026-10-30\\"), string("..\\v36-cores-2026-10-30\\")}) if (fs::exists(d + n)) return d + n; return string(); }
static bool build_input(const string &name, uint64_t target) {
    if (fs::exists(name) && fs::file_size(name) == target) return true;
    FILE *o = fopen(name.c_str(), "wb"); if (!o) return false; uint64_t w = 0; vector<uint8_t> b(1 << 22);
    while (w < target) for (auto &n : PARTS) {
        string p = findp(n); if (p.empty()) { fclose(o); logf("STOPPED: Silesia file %s not found\n", n.c_str()); return false; }
        FILE *f = fopen(p.c_str(), "rb"); size_t k;
        while (w < target && (k = fread(b.data(), 1, (size_t)min<uint64_t>(b.size(), target - w), f)) > 0) { fwrite(b.data(), 1, k, o); w += k; }
        fclose(f); if (w >= target) break;
    }
    fclose(o); return true;
}
static void rm(const string &n) { error_code ec; fs::remove(n, ec); }
static uint64_t fsz(const string &n) { error_code ec; auto s = fs::file_size(n, ec); return ec ? 0 : s; }
struct One { double sec, mb; bool ok; };
static One dec(const string &cmd, const string &out, uint64_t L0, uint32_t C0) {
    rm(out); Res r = run(cmd); uint64_t L = 0; uint32_t C = 0; bool ok = !r.code && file_crc(out, L, C) && L == L0 && C == C0; rm(out); return {r.sec, r.peakMB, ok};
}
int main() {
    crc_init(); LOG = fopen("results.txt", "a"); if (!LOG) return 1;
    logf("\nhs_zstd CORPUS + MEMORY TEST (zstd 1.5.6, LZ4 1.10.0, hs_zstd 1.1 / 1.0, 12 jobs unless stated)\n");
    // ---------- (A) corpus ----------
    struct Tool { const char *name; string ext, comp, decomp; };
    vector<Tool> tools = {
        {"LZ4 default", ".lz4", "lz4.exe -q -f {in} {z}", "lz4.exe -q -d -f {z} {o}"},
        {"zstd default", ".zst", "zstd.exe -q -f {in} -o {z}", "zstd.exe -q -d -f {z} -o {o}"},
        {"hs_zstd 16 MB", ".h16", "hs_zstd.exe c {in} {z} --jobs 12 --block-mb 16 >nul", "hs_zstd.exe d {z} {o} --jobs 12 >nul"},
        {"hs_zstd 4 MB", ".h4", "hs_zstd.exe c {in} {z} --jobs 12 --block-mb 4 >nul", "hs_zstd.exe d {z} {o} --jobs 12 >nul"}};
    auto sub = [](string c, const string &in, const string &z, const string &o) { for (auto [k, v] : vector<pair<string, string>>{{"{in}", in}, {"{z}", z}, {"{o}", o}}) for (size_t p; (p = c.find(k)) != string::npos;) c.replace(p, k.size(), v); return c; };
    logf("\n(A) 12 Silesia files, one at a time. Per file: compressed bytes / compress s / median decode s (3 runs) / peak decode MB\n");
    logf("file | original | LZ4 | zstd | hs_zstd 16 MB | hs_zstd 4 MB\n");
    vector<uint64_t> zt(tools.size(), 0); vector<double> ct(tools.size(), 0), dt(tools.size(), 0), pk(tools.size(), 0); uint64_t T0 = 0; int bad = 0;
    for (auto &f : PARTS) {
        string in = findp(f); if (in.empty()) { logf("STOPPED: %s not found\n", f.c_str()); return 1; }
        uint64_t L0; uint32_t C0; file_crc(in, L0, C0); T0 += L0; string line = f + " | " + fmt(L0);
        for (size_t t = 0; t < tools.size(); t++) {
            string z = "c" + tools[t].ext; rm(z); Res c = run(sub(tools[t].comp, in, z, "")); uint64_t zs = fsz(z);
            vector<double> d; double mb = 0;
            for (int r = 0; r < 3; r++) { One x = dec(sub(tools[t].decomp, "", z, "d.out"), "d.out", L0, C0); d.push_back(x.sec); mb = max(mb, x.mb); bad += !x.ok || c.code; }
            double md = median(d); zt[t] += zs; ct[t] += c.sec; dt[t] += md; pk[t] = max(pk[t], mb); rm(z);
            char b[160]; snprintf(b, sizeof b, " | %s / %.2f / %.3f / %.0f", fmt(zs).c_str(), c.sec, md, mb); line += b;
        }
        logf("%s\n", line.c_str());
    }
    logf("\nTOTALS (12 files, %s bytes) | 1,000,000 bits down to | compress s | summed median decode s | decode multiple M vs LZ4 | highest decode peak MB\n", fmt(T0).c_str());
    for (size_t t = 0; t < tools.size(); t++) logf("%s | %.0f | %.1f | %.3f | %.2f | %.0f\n", tools[t].name, 1e6 * zt[t] / T0, ct[t], dt[t], dt[t] / dt[0], pk[t]);
    logf("failures: %d\n", bad);
    // ---------- (B) memory, 2 GB ----------
    logf("\n(B) MEMORY - 2 GB Silesia repeated, 2 decode runs each (decode s run 1 / run 2, peak MB)\n");
    if (!build_input("in2g.bin", 2000000000ULL)) return 1; uint64_t L0; uint32_t C0; file_crc("in2g.bin", L0, C0);
    rm("p.zst"); Res zc = run("zstd.exe -q -f in2g.bin -o p.zst");
    { One a = dec("zstd.exe -q -d -f p.zst -o z.out", "z.out", L0, C0), b = dec("zstd.exe -q -d -f p.zst -o z.out", "z.out", L0, C0);
      logf("zstd default | %s bytes | compress %.1f s | decode %.2f / %.2f | peak %.0f / %.0f MB | %s\n", fmt(fsz("p.zst")).c_str(), zc.sec, a.sec, b.sec, a.mb, b.mb, a.ok && b.ok ? "exact" : "NO"); }
    rm("p.zst");
    struct Cfg { int mb; const char *exe; int jobs; } cfgs[] = {{16, "hs_zstd10.exe", 12}, {16, "hs_zstd.exe", 12}, {16, "hs_zstd.exe", 6}, {8, "hs_zstd.exe", 12}, {4, "hs_zstd.exe", 12}};
    int lastmb = 0; uint64_t zs = 0; double cs = 0;
    for (auto &c : cfgs) {
        if (c.mb != lastmb) { rm("h.hsz"); Res r = run("hs_zstd.exe c in2g.bin h.hsz --jobs 12 --block-mb " + to_string(c.mb) + " >nul"); zs = fsz("h.hsz"); cs = r.sec; lastmb = c.mb; }
        string cmd = string(c.exe) + " d h.hsz h.out --jobs " + to_string(c.jobs) + " >nul";
        One a = dec(cmd, "h.out", L0, C0), b = dec(cmd, "h.out", L0, C0);
        logf("%s, %d MB blocks, %d jobs | %s bytes (%.0f) | compress %.1f s | decode %.2f / %.2f | peak %.0f / %.0f MB | %s\n", strcmp(c.exe, "hs_zstd.exe") ? "hs_zstd 1.0" : "hs_zstd 1.1", c.mb, c.jobs, fmt(zs).c_str(), 1e6 * zs / L0, cs, a.sec, b.sec, a.mb, b.mb, a.ok && b.ok ? "exact" : "NO");
    }
    rm("h.hsz"); rm("in2g.bin"); logf("ALL DONE (big files deleted)\n"); fclose(LOG); return 0;
}
