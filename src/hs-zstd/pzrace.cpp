// pzrace.cpp - hs_zstd 1.1 vs pzstd (zstd's own parallel tool, zstd 1.5.6 contrib) vs plain zstd, level 3 everywhere,
// 12 threads for the parallel tools. 2 GB (3 decode rounds) and 10 GB (2 decode rounds) of Silesia repeated. Decode
// order rotates every round; 10 s pause before each timed decode; every output CRC-checked (not timed) and deleted.
// Michael Hugh Harrington, 2026. MIT License.
// Build: zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static pzrace.cpp -o pzrace.exe
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
    logf("\nRACE: hs_zstd 1.1 vs pzstd vs plain zstd (zstd 1.5.6, level 3, 12 threads for the parallel tools)\n");
    ULARGE_INTEGER fb{}; GetDiskFreeSpaceExA(".", &fb, nullptr, nullptr);
    if (fb.QuadPart < 40ULL * 1000000000ULL) { logf("STOPPED: only %s bytes free; need about 40 GB.\n", fmt(fb.QuadPart).c_str()); return 1; }
    struct T { const char *name, *file, *comp, *dec; };
    T tools[] = {{"zstd (1 core)", "a.zst", "zstd.exe -q -f in.bin -o a.zst", "zstd.exe -q -d -f a.zst -o out.bin"},
                 {"pzstd -p 12", "b.pz", "pzstd.exe -q -p 12 -3 -f in.bin -o b.pz", "pzstd.exe -q -d -p 12 -f b.pz -o out.bin"},
                 {"hs_zstd 16 MB", "c.hsz", "hs_zstd.exe c in.bin c.hsz --jobs 12 --block-mb 16 >nul", "hs_zstd.exe d c.hsz out.bin --jobs 12 >nul"},
                 {"hs_zstd 4 MB", "d.hsz", "hs_zstd.exe c in.bin d.hsz --jobs 12 --block-mb 4 >nul", "hs_zstd.exe d d.hsz out.bin --jobs 12 >nul"}};
    const int NT = 4;
    for (auto [size, rounds] : vector<pair<uint64_t, int>>{{2000000000ULL, 3}, {10000000000ULL, 2}}) {
        rm("in.bin"); if (!build_input("in.bin", size)) return 1; uint64_t L0; uint32_t C0; file_crc("in.bin", L0, C0);
        logf("\n--- %.0f GB (%s bytes, CRC-32 %08X) ---\n", size / 1e9, fmt(L0).c_str(), C0);
        Res mt = run("zstd.exe -q -f -T0 in.bin -o mt.zst"); logf("zstd -T0 (its own multi-core compress, same file format as plain zstd): %.1f s, %s bytes\n", mt.sec, fmt(fsz("mt.zst")).c_str()); rm("mt.zst");
        logf("tool | compressed bytes | 1,000,000 bits down to | compress s | compress peak MB\n");
        for (int t = 0; t < NT; t++) { rm(tools[t].file); Res c = run(tools[t].comp); uint64_t z = fsz(tools[t].file);
            logf("%s | %s | %.0f | %.1f | %.0f%s\n", tools[t].name, fmt(z).c_str(), 1e6 * z / L0, c.sec, c.peakMB, c.code ? " | COMPRESS FAILED" : ""); }
        vector<vector<double>> d(NT); vector<double> pk(NT, 0); int bad = 0;
        logf("round | order | decode s: zstd / pzstd / hs_zstd 16 MB / hs_zstd 4 MB | all exact\n");
        for (int r = 0; r < rounds; r++) {
            string order; double tt[4] = {0}; bool okall = true;
            for (int j = 0; j < NT; j++) { int t = (j + r) % NT; order += (j ? ">" : "") + string(tools[t].name).substr(0, 7);
                this_thread::sleep_for(chrono::seconds(10)); One x = dec(tools[t].dec, "out.bin", L0, C0); tt[t] = x.sec; d[t].push_back(x.sec); pk[t] = max(pk[t], x.mb); if (!x.ok) { okall = false; bad++; } }
            logf("%d | %s | %.2f / %.2f / %.2f / %.2f | %s\n", r + 1, order.c_str(), tt[0], tt[1], tt[2], tt[3], okall ? "yes" : "NO");
        }
        logf("median decode | %.2f / %.2f / %.2f / %.2f s | peak MB %.0f / %.0f / %.0f / %.0f | failures %d\n", median(d[0]), median(d[1]), median(d[2]), median(d[3]), pk[0], pk[1], pk[2], pk[3], bad);
        logf("speed vs plain zstd: pzstd %.2fx, hs_zstd 16 MB %.2fx, hs_zstd 4 MB %.2fx | hs_zstd 4 MB vs pzstd %.2fx\n", median(d[0]) / median(d[1]), median(d[0]) / median(d[2]), median(d[0]) / median(d[3]), median(d[1]) / median(d[3]));
        for (int t = 0; t < NT; t++) rm(tools[t].file); rm("in.bin");
    }
    logf("ALL DONE (big files deleted)\n"); fclose(LOG); return 0;
}
