// pigzrace.cpp - pigz 2.8 vs gzip -6 vs hs_wrap 1.4 (direct decode) on 2 GB of Silesia repeated, 3 decode rounds, order rotated.
// Derived from gzsteps.cpp. Original header follows.
// gzsteps.cpp - wrapper 1.4 direct-decode test: gzip -6 (zlib 1.3.1 minigzip) alone vs wrapper 1.3 (temporary
// files) vs wrapper 1.4 (direct: blocks piped into gzip, no temporary files), on 0.5, 1 and 2 GB of Silesia
// repeated (3 interleaved decompress runs each). If 1.4 decompresses faster than gzip alone at all three sizes, it
// goes on to 10 GB: 2 full round trips each of gzip alone and wrapper 1.4. Every run is timed (wall clock), peak
// committed memory comes from a Windows job object, every decoded file is checked by length and CRC-32.
// Results go to results.txt; everything stays in this folder and big files are deleted at the end.
// Michael Hugh Harrington, 2026. MIT License.
// Build: zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static gzsteps.cpp -o gzsteps.exe
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
    const int jobs = 12; const string J = " --jobs " + to_string(jobs);
    logf("\nPIGZ RACE - pigz 2.8 (-p %d) vs gzip -6 (zlib minigzip) vs hs_wrap 1.4 + gzip -6 (%d jobs, 16 MB blocks), 2 GB Silesia repeated, 3 decode rounds, order rotated\n", jobs, jobs);
    { // self-test 3 MB
        string src = findp("samba"); if (src.empty()) { logf("STOPPED: Silesia samba not found\n"); return 1; }
        FILE *f = fopen(src.c_str(), "rb"); vector<uint8_t> b(3 << 20); size_t k = fread(b.data(), 1, b.size(), f); fclose(f);
        FILE *o = fopen("st.bin", "wb"); fwrite(b.data(), 1, k, o); fclose(o); uint64_t L0; uint32_t C0; file_crc("st.bin", L0, C0);
        Res c1 = run("pigz.exe -6 -p 12 -c st.bin > st.pz"); One a = dec("pigz.exe -d -c -p 12 st.pz > st.out", "st.out", L0, C0);
        Res c2 = run("gzip.exe -6 -c st.bin > st.gz"); One b2 = dec("gzip.exe -d -c st.gz > st2.out", "st2.out", L0, C0);
        Res c3 = run("hs_wrap14.exe c st.bin st.hsw --comp \"gzip.exe -6 -c {in}\" --jobs 4 --block-mb 1"); One b3 = dec("hs_wrap14.exe d st.hsw st3.out --decomp \"gzip.exe -d -c\" --jobs 4", "st3.out", L0, C0);
        logf("self-test (3 MB): pigz %s (%s bytes), gzip %s, wrapper %s\n", !c1.code && a.ok ? "exact" : "FAILED", fmt(fsz("st.pz")).c_str(), !c2.code && b2.ok ? "exact" : "FAILED", !c3.code && b3.ok ? "exact" : "FAILED");
        rm("st.bin"); rm("st.pz"); rm("st.gz"); rm("st.hsw"); if (c1.code || !a.ok || c2.code || !b2.ok || c3.code || !b3.ok) { logf("STOPPED: self-test failed\n"); return 1; }
    }
    ULARGE_INTEGER fb{}; GetDiskFreeSpaceExA(".", &fb, nullptr, nullptr);
    if (fb.QuadPart < 6ULL * 1000000000ULL) { logf("STOPPED: only %s bytes free; need about 6 GB.\n", fmt(fb.QuadPart).c_str()); return 1; }
    const uint64_t T = 2000000000ULL; string in = "in.bin"; rm(in); if (!build_input(in, T)) return 1; uint64_t L0; uint32_t C0; file_crc(in, L0, C0);
    logf("input: %s bytes, CRC-32 %08X\n", fmt(L0).c_str(), C0);
    struct Tool { string name, z, o, comp, decomp; } tools[3] = {
        {"gzip -6 (1 core)", "plain.gz", "p.out", "gzip.exe -6 -c in.bin > plain.gz", "gzip.exe -d -c plain.gz > p.out"},
        {"pigz -6 -p 12", "pigz.gz", "z.out", "pigz.exe -6 -p 12 -c in.bin > pigz.gz", "pigz.exe -d -c -p 12 pigz.gz > z.out"},
        {"hs_wrap 1.4 + gzip -6, 16 MB", "wrap.hsw", "w.out", "hs_wrap14.exe c in.bin wrap.hsw --comp \"gzip.exe -6 -c {in}\"" + J + " --block-mb 16", "hs_wrap14.exe d wrap.hsw w.out --decomp \"gzip.exe -d -c\"" + J}};
    logf("tool | compressed bytes | 1,000,000 bits down to | compress s | compress peak MB\n");
    Res cr[3]; for (int i = 0; i < 3; i++) { rm(tools[i].z); cr[i] = run(tools[i].comp); logf("%s | %s | %.0f | %.1f | %.0f%s\n", tools[i].name.c_str(), fmt(fsz(tools[i].z)).c_str(), 1e6 * fsz(tools[i].z) / L0, cr[i].sec, cr[i].peakMB, cr[i].code ? " COMPRESS FAILED" : ""); }
    logf("round | order | decode s: gzip / pigz / hs_wrap | peak MB | all exact\n");
    vector<double> t[3], m[3]; int bad = 0;
    for (int r = 0; r < 3; r++) {
        One res[3]; string order;
        for (int k = 0; k < 3; k++) { int i = (k + r) % 3; order += (k ? " > " : "") + tools[i].name.substr(0, 4); Sleep(5000); res[i] = dec(tools[i].decomp, tools[i].o, L0, C0); }
        for (int i = 0; i < 3; i++) { t[i].push_back(res[i].sec); m[i].push_back(res[i].mb); bad += !res[i].ok; }
        logf("%d | %s | %.2f / %.2f / %.2f | %.0f / %.0f / %.0f | %s\n", r + 1, order.c_str(), res[0].sec, res[1].sec, res[2].sec, res[0].mb, res[1].mb, res[2].mb, (res[0].ok && res[1].ok && res[2].ok) ? "yes" : "NO");
    }
    logf("median decode | %.2f / %.2f / %.2f s | peak MB %.0f / %.0f / %.0f | failures %d\n", median(t[0]), median(t[1]), median(t[2]), median(m[0]), median(m[1]), median(m[2]), bad);
    logf("decode speed vs gzip: pigz %.2fx, hs_wrap %.2fx | hs_wrap vs pigz %.2fx\n", median(t[0]) / median(t[1]), median(t[0]) / median(t[2]), median(t[1]) / median(t[2]));
    rm(in); rm("plain.gz"); rm("pigz.gz"); rm("wrap.hsw"); logf("ALL DONE (big files deleted)\n"); fclose(LOG); return bad ? 1 : 0;
}
