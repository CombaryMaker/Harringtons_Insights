// hxab.cpp - HX Ablation Test: run HX with each of Michael Harrington's stages on, or replaced by a published alternative.
// For scientific and peer review purposes only. Patent Pending (U.S. Provisional App. 64/192,843).
// Copyright (c) 2026 Michael Hugh Harrington. Same license as HX (HX/LICENSE.txt): research and peer review allowed.
//
// Rules built into the program: every result is decoded with the normal HX decoder and compared byte for byte (SHA-256);
// every number in the log is measured (no estimates); replacements are plain published methods (see the ? buttons).
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace std;
typedef wstring W;
static const wchar_t *TITLE = L"For scientific and peer review purposes only, HX Ablation Test, Patent Pending";
static const wchar_t *VERSION = L"HX Ablation Test 1.0";

// ------------------------------------------------------------------ the stages and their replacements
struct Opt { const wchar_t *name, *help; const wchar_t *hxflag; const wchar_t *pre; const wchar_t *preArgs; };
// hxflag: switches added to "hx c"; pre: outside program run before HX (forward "f", inverse "i" after decoding)
struct Stage { const wchar_t *label, *mine, *stageHelp, *mineHelp; vector<Opt> opts; bool locked; };
static vector<Stage> ST;
static void build_stages() {
    ST = {
    {L"Decide: H Sampler", L"H Sampler (Harrington)",
     L"DECIDE - H SAMPLER\r\n\r\nHX has many ways (\"lines\") to code each 1 MB piece of a file. Trying all of them in full is slow. This "
     L"stage decides which lines are worth running in full.\r\n\r\nIt only affects speed and which line is chosen; every choice still decodes "
     L"to the same file.",
     L"H SAMPLER (Michael Hugh Harrington, Patent Pending)\r\n\r\nEvery line is tried on small sample slices of the piece. Only the lines that "
     L"come within 3% of the best sample (at most 6) are then run on the whole piece, and the real full-size results decide the winner.",
     {{L"Try every line in full", L"TRY EVERY LINE IN FULL (exhaustive search)\r\n\r\nNo sampling: every line is run on every whole piece and the "
       L"smallest result is kept. The slowest and most thorough way to choose. Standard exhaustive search.", L"--full", nullptr, nullptr},
      {L"Sample picks the winner (BtrBlocks style)", L"SAMPLE PICKS THE WINNER\r\n\r\nLines are tried on samples and the best sample's line is used "
       L"directly, with no full-size final check. This is how BtrBlocks chooses schemes (Kuschewski et al., SIGMOD 2023).", L"--abl-pick1", nullptr, nullptr}}, false},
    {L"Decide: H-Direct router", L"H-Direct router (Harrington)",
     L"DECIDE - ROUTER (HX Complete only)\r\n\r\nHX Complete can also compress the whole file with other strong methods (BSC, bzip3, LZMA, PPMd, "
     L"Brotli). The router predicts which of these to try first.",
     L"H-DIRECT ROUTER\r\n\r\nThe learned router needs its training table, which is not part of this public build. In this build HX Complete "
     L"always tries every whole-file method, so this stage is the same in every run.", {}, true},
    {L"Group: Harrington's Sets", L"Harrington's Sets (Harrington)",
     L"GROUP\r\n\r\nSorts the 64 KB windows of a file into groups of similar data before coding, so similar data is coded together and each "
     L"group can use its own core. A small map puts every window back in place.",
     L"HARRINGTON'S SETS (Michael Hugh Harrington, Patent Pending; grouping itself follows McDaniel & Heydari 2003 and Li et al. 2005)\r\n\r\n"
     L"Windows are classed by their byte statistics into up to 4 classes, coded as separate streams. Kept only when smaller.",
     {{L"Plain fixed blocks", L"PLAIN FIXED BLOCKS\r\n\r\nNo grouping: the file is cut into fixed 1 MB pieces in its original order, the way "
       L"most block compressors work (zstd -T, pzstd, bzip2).", L"--abl-sets-off", nullptr, nullptr},
      {L"File-type grouping (7-Zip solid style)", L"FILE-TYPE GROUPING\r\n\r\nEach 64 KB window is labelled text, program, random-looking or "
       L"other by simple fixed rules, like archivers that group files by type (7-Zip, RAR solid archives). Program: typegroup.exe.", L"gmap", L"typegroup.exe", nullptr},
      {L"Byte-histogram clustering (fileprints)", L"BYTE-HISTOGRAM CLUSTERING (fileprints)\r\n\r\nWindows are clustered by their byte "
       L"histograms with k-means (4 groups), as in McDaniel & Heydari 2003 and Li, Wang, Stolfo & Herzog 2005. Program: histcluster.exe.", L"gmap", L"histcluster.exe", nullptr}}, false},
    {L"Order: H Processor", L"H Processor (Harrington)",
     L"ORDER\r\n\r\nInside each group, puts windows next to the windows most like them, so the coder finds more repeats.",
     L"H PROCESSOR\r\n\r\nNot part of this public build. Windows keep their original order in every run, so this stage is the same in every "
     L"run.", {}, true},
    {L"Transform: Exchange lines", L"Harrington Exchange lines (Harrington)",
     L"TRANSFORM\r\n\r\nReshapes the data before the entropy coder so its patterns are easier to code, for example by splitting numbers into "
     L"lanes or predicting each value from its neighbours.",
     L"HARRINGTON EXCHANGE LINES (Michael Hugh Harrington, Patent Pending)\r\n\r\nFor every 1 MB piece, HX tries its own set of reversible "
     L"lines (byte lanes, 12-bit samples, record lanes, image, audio and context lines, value exchange) and keeps the smallest.",
     {{L"None: plain entropy coding", L"NONE - PLAIN ENTROPY CODING\r\n\r\nNo transform: each piece's bytes go straight to the standard order-0 "
       L"entropy coders (Huff0, FSE or rANS, whichever is smallest).", L"--abl-lines-off", nullptr, nullptr},
      {L"Byte shuffle (Blosc / HDF5)", L"BYTE SHUFFLE\r\n\r\nSplits 2-byte values into their byte lanes, as the HDF5 and Blosc shuffle filter "
       L"does, then plain entropy coding. Program: shuffle.exe.", L"--abl-lines-off", L"shuffle.exe", L"-w 2"},
      {L"Bitshuffle (Masui et al. 2015)", L"BITSHUFFLE\r\n\r\nSplits 2-byte values into bit planes (Masui et al. 2015), then plain entropy coding. "
       L"Program: bitshuffle.exe.", L"--abl-lines-off", L"bitshuffle.exe", L"-w 2"},
      {L"Delta (xz / 7-Zip delta filter)", L"DELTA\r\n\r\nEach byte is replaced by its difference from the byte before it, as the xz and 7-Zip "
       L"delta filter does, then plain entropy coding. Program: delta.exe.", L"--abl-lines-off", L"delta.exe", L"-d 1"},
      {L"x86 BCJ (xz / 7-Zip)", L"x86 BCJ\r\n\r\nConverts relative jump addresses in x86 programs to absolute ones (LZMA SDK, Igor Pavlov), then "
       L"plain entropy coding. Program: bcj.exe.", L"--abl-lines-off", L"bcj.exe", L""},
      {L"Dictionary encoding (Parquet)", L"DICTIONARY ENCODING\r\n\r\n2-byte values are replaced by their index in a dictionary of distinct values, "
       L"as in Apache Parquet, then plain entropy coding. Program: dict.exe.", L"--abl-lines-off", L"dict.exe", L"-w 2"}}, false},
    {L"Leftovers: Cascade repeat line", L"Cascade repeat line (Harrington)",
     L"LEFTOVERS\r\n\r\nHandles what the lines do not code well, especially long repeats far apart in a file.",
     L"CASCADE REPEAT LINE (Michael Hugh Harrington, Patent Pending)\r\n\r\nIn this build the cascade's repeat line codes the whole file with "
     L"long-distance matching (zstd level 19, 128 MB window) when an estimate says it can win, and is kept only when smaller.",
     {{L"Off (pieces stay with their own coders)", L"OFF\r\n\r\nNo repeat line: every piece stays with the transform stage's coders, and pieces "
       L"that do not shrink are stored as they are (like stored blocks in Deflate and zstd).", L"--abl-repeat-off", nullptr, nullptr},
      {L"Block deduplication (FastCDC)", L"BLOCK DEDUPLICATION\r\n\r\nRepeated chunks are found with content-defined chunking (FastCDC, Xia et al. "
       L"2016) and stored once, as backup tools and srep/ZPAQ do; the repeat line is off. Program: dedup.exe.", L"--abl-repeat-off", L"dedup.exe", L""}}, false},
    {L"Route: Split route", L"Split route (Harrington)",
     L"ROUTE\r\n\r\nDecides whether one file can be split, so that some pieces go to one coder and the rest to another, inside one container.",
     L"SPLIT ROUTE (Michael Hugh Harrington, Patent Pending)\r\n\r\nPieces where the Exchange lines clearly win go to them; everything else "
     L"stays together in one stream for a general coder (zstd long, BSC or bzip3). Kept only when smaller.",
     {{L"Whole-file best-of (keep the smallest)", L"WHOLE-FILE BEST-OF\r\n\r\nNo split: each method codes the whole file and the smallest result "
       L"is kept, as paq8-style and \"try several, keep the best\" tools do.", L"--abl-split-off", nullptr, nullptr}}, false},
    };
}

// ------------------------------------------------------------------ helpers
static W g_dir, g_tools, g_tmp;
static W fmtn(long long v) { W s = to_wstring(v < 0 ? -v : v); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L","); return v < 0 ? L"-" + s : s; }
static W fmtd(double v, int dp) { wchar_t b[64]; swprintf(b, 64, L"%.*f", dp, v); return b; }
static W human(long long b) { if (b < 1000) return fmtn(b) + L" bytes"; const wchar_t *u[] = {L"KB", L"MB", L"GB", L"TB"}; double v = (double)b; int k = -1; while (v >= 1000 && k < 3) { v /= 1000; k++; } return fmtd(v, 2) + L" " + u[k]; }
static W pct(double v) { return fmtd(v, v < 1 ? 3 : v < 10 ? 2 : 1) + L"%"; }
static long long fsize(const W &f) { WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &a)) return -1; if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1; return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow; }
static W basename(const W &p) { size_t k = p.find_last_of(L"\\/"); return k == W::npos ? p : p.substr(k + 1); }
static int cores() { DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); return n ? (int)n : 1; }
static double median(vector<double> v) { if (v.empty()) return 0; sort(v.begin(), v.end()); size_t k = v.size(); return k % 2 ? v[k/2] : (v[k/2-1] + v[k/2]) / 2; }
static W utf16(const string &s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0); W w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }

struct Sha { uint32_t h[8]; uint64_t n = 0; uint8_t b[64]; int k = 0;
    Sha() { static const uint32_t i[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}; memcpy(h, i, 32); }
    static uint32_t r(uint32_t x, int c) { return x >> c | x << (32 - c); }
    void blk(const uint8_t *p) { static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64]; for (int i = 0; i < 16; i++) w[i] = p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
        for (int i = 16; i < 64; i++) w[i] = w[i-16] + (r(w[i-15],7) ^ r(w[i-15],18) ^ w[i-15] >> 3) + w[i-7] + (r(w[i-2],17) ^ r(w[i-2],19) ^ w[i-2] >> 10);
        uint32_t a=h[0],b2=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) { uint32_t t1 = hh + (r(e,6)^r(e,11)^r(e,25)) + ((e&f)^(~e&g)) + K[i] + w[i], t2 = (r(a,2)^r(a,13)^r(a,22)) + ((a&b2)^(a&c)^(b2&c));
            hh=g; g=f; f=e; e=d+t1; d=c; c=b2; b2=a; a=t1+t2; }
        h[0]+=a; h[1]+=b2; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh; }
    void add(const uint8_t *p, size_t m) { n += m; while (m) { int c = (int)min<size_t>(64 - k, m); memcpy(b + k, p, c); k += c; p += c; m -= c; if (k == 64) { blk(b); k = 0; } } }
    W hex() { uint64_t bits = n * 8; uint8_t pad = 0x80; add(&pad, 1); uint8_t z = 0; while (k != 56) add(&z, 1);
        uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8*i)); add(L, 8);
        wchar_t s[65]; for (int i = 0; i < 8; i++) swprintf(s + 8*i, 9, L"%08x", h[i]); return s; }
};
static W sha_file(const W &f) { HANDLE h = CreateFileW(f.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return L"(unreadable)"; Sha s; vector<uint8_t> b(1 << 20); DWORD got;
    while (ReadFile(h, b.data(), (DWORD)b.size(), &got, NULL) && got) s.add(b.data(), got); CloseHandle(h); return s.hex(); }

// ------------------------------------------------------------------ running programs (timed, memory and CPU measured, cancellable)
static atomic<bool> g_cancel(false); static HANDLE g_curJob = NULL; static CRITICAL_SECTION g_jobLock;
struct Run { bool ok = false; double sec = 0, cpu = 0, peakMB = 0; DWORD code = 0; string out; };
static Run run_cmd(const W &cmdline, bool debugEnv) {
    Run r; SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    W logf = g_tmp + L"child-output.txt"; DeleteFileW(logf.c_str());
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE out = CreateFileW(logf.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    STARTUPINFOW si = {sizeof si}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = out; si.hStdError = out;
    PROCESS_INFORMATION pi; vector<wchar_t> cl(cmdline.begin(), cmdline.end()); cl.push_back(0);
    if (debugEnv) SetEnvironmentVariableW(L"HX_DEBUG", L"1"); else SetEnvironmentVariableW(L"HX_DEBUG", NULL);
    HANDLE job = CreateJobObjectW(NULL, NULL); LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    if (CreateProcessW(NULL, cl.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, g_tmp.c_str(), &si, &pi)) {
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        EnterCriticalSection(&g_jobLock); g_curJob = job; LeaveCriticalSection(&g_jobLock);
        QueryPerformanceCounter(&t0); ResumeThread(pi.hThread);
        if (!g_cancel) WaitForSingleObject(pi.hProcess, INFINITE); else TerminateJobObject(job, 1);
        WaitForSingleObject(pi.hProcess, INFINITE); QueryPerformanceCounter(&t1);
        EnterCriticalSection(&g_jobLock); g_curJob = NULL; LeaveCriticalSection(&g_jobLock);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl = {}; JOBOBJECT_BASIC_ACCOUNTING_INFORMATION ja = {};
        if (job && QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &jl, sizeof jl, NULL)) r.peakMB = jl.PeakJobMemoryUsed / 1e6;
        if (job && QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &ja, sizeof ja, NULL)) r.cpu = (ja.TotalUserTime.QuadPart + ja.TotalKernelTime.QuadPart) / 1e7;
        GetExitCodeProcess(pi.hProcess, &r.code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        r.sec = double(t1.QuadPart - t0.QuadPart) / f.QuadPart; r.ok = r.code == 0 && !g_cancel;
    } else r.out = "could not start the program";
    if (job) CloseHandle(job); if (out != INVALID_HANDLE_VALUE) CloseHandle(out); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    FILE *fp = _wfopen(logf.c_str(), L"rb"); if (fp) { char b[4096]; size_t m; while ((m = fread(b, 1, sizeof b, fp)) > 0 && r.out.size() < 200000) r.out.append(b, m); fclose(fp); }
    DeleteFileW(logf.c_str()); return r;
}
static W q(const W &s) { return L"\"" + s + L"\""; }

// ------------------------------------------------------------------ window state
enum { ID_RADIO = 1000, ID_COMBO = 2000, ID_HSTAGE = 3000, ID_HOPT = 4000, ID_ENALL = 101, ID_DISALL, ID_FAST, ID_COMPLETE, ID_LOAD, ID_FOLDER,
       ID_FOLDERCHK, ID_RUNS, ID_BASIC, ID_ADV, ID_RUN, ID_CANCEL, ID_SAVE, ID_LOG, ID_STATUS, ID_TITLE, ID_FILE, ID_HMODE };
#define WM_STATUS (WM_APP + 1)
#define WM_DONE (WM_APP + 2)
static HWND g_wnd, g_title, g_enall, g_disall, g_fast, g_complete, g_hmode, g_load, g_folder, g_folderchk, g_file, g_runs, g_basic, g_adv, g_run, g_cancelb, g_save, g_log, g_status;
struct RowW { HWND label, hs, on, off, combo, ho; }; static vector<RowW> g_rows; static HWND g_lbl[4];
static HFONT g_font, g_bold, g_titleFont, g_mono; static int g_dpi = 96; static int S(int v) { return MulDiv(v, g_dpi, 96); }
static HBRUSH g_panelBrush; static COLORREF PANEL = RGB(233, 233, 233); static RECT g_panels[3], g_titleRect;
static W g_path, g_folderPath, g_logText; static bool g_busy = false;
static int g_runsN = 5; static bool g_advanced = false, g_complete_mode = false, g_useFolder = false;
struct Choice { bool on; int opt; }; static vector<Choice> g_choice;

static bool opt_available(const Opt &o) { return !o.pre || fsize(g_tools + o.pre) >= 0; }
static void fill_combo(size_t i) {
    HWND c = g_rows[i].combo; SendMessageW(c, CB_RESETCONTENT, 0, 0); const Stage &s = ST[i];
    if (s.locked) { SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Not in this build (same in every run)"); SendMessageW(c, CB_SETCURSEL, 0, 0); return; }
    for (auto &o : s.opts) { W t = o.name; if (!opt_available(o)) t += L"  (program not installed)"; SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)t.c_str()); }
    SendMessageW(c, CB_SETCURSEL, 0, 0);
}
static void sync_row(size_t i) {
    const Stage &s = ST[i]; bool on = s.locked || SendMessageW(g_rows[i].on, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(g_rows[i].combo, !s.locked && !on && !g_busy); EnableWindow(g_rows[i].on, !s.locked && !g_busy); EnableWindow(g_rows[i].off, !s.locked && !g_busy);
}
static void set_all(bool on) { for (size_t i = 0; i < ST.size(); i++) { if (ST[i].locked) continue; SendMessageW(g_rows[i].on, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(g_rows[i].off, BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0); sync_row(i); } }
static void set_status(const W &s) { SetWindowTextW(g_status, s.c_str()); }
static void post_status(const W &s) { PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)new W(s)); }

// ------------------------------------------------------------------ machine description (measured / reported by Windows)
static W machine_info() {
    W cpu = L"unknown CPU"; HKEY k; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t b[256]; DWORD sz = sizeof b; if (RegQueryValueExW(k, L"ProcessorNameString", NULL, NULL, (LPBYTE)b, &sz) == ERROR_SUCCESS) { cpu = b; while (!cpu.empty() && cpu.back() == L' ') cpu.pop_back(); } RegCloseKey(k); }
    MEMORYSTATUSEX ms = {sizeof ms}; GlobalMemoryStatusEx(&ms);
    typedef LONG (WINAPI *RGV)(PRTL_OSVERSIONINFOW); RTL_OSVERSIONINFOW vi = {sizeof vi}; RGV f = (RGV)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); if (f) f(&vi);
    return cpu + L", " + to_wstring(cores()) + L" logical cores, " + fmtd(ms.ullTotalPhys / 1e9, 1) + L" GB RAM, Windows " + to_wstring(vi.dwMajorVersion) + L"." + to_wstring(vi.dwMinorVersion) + L" build " + to_wstring(vi.dwBuildNumber);
}

// ------------------------------------------------------------------ one configuration on one file
struct Res { W file; long long orig = -1, comp = -1; bool exact = false, failed = false; W err; vector<double> ct, dt, cc, dc; double cmem = 0, dmem = 0; W shaIn, shaOut; W adv; };
static W settings_code(const vector<Choice> &c, bool complete) {
    W s = L"HXAB1-"; s += complete ? L"C" : L"F"; for (size_t i = 0; i < c.size(); i++) { s += L"-"; s += ST[i].locked ? L"x" : c[i].on ? L"H" : to_wstring(c[i].opt + 1); } return s;
}
static W plain_settings(const vector<Choice> &c, bool complete) {
    W s = W(L"Mode: ") + (complete ? L"HX Complete" : L"HX Fast") + L"\r\n";
    for (size_t i = 0; i < ST.size(); i++) { s += L"  " + W(ST[i].label) + L": "; s += ST[i].locked ? L"not in this build (same in every run)" : c[i].on ? W(ST[i].mine) : W(L"OFF -> ") + ST[i].opts[c[i].opt].name; s += L"\r\n"; }
    int n = 0, m = 0; for (size_t i = 0; i < ST.size(); i++) if (!ST[i].locked) { m++; if (c[i].on) n++; }
    s += L"  " + to_wstring(n) + L" of " + to_wstring(m) + L" Harrington stages on\r\n"; return s;
}
static Res run_one(const W &in, const vector<Choice> &ch, bool complete, int runs, bool adv, int fileNo, int fileCount) {
    Res R; R.file = in; R.orig = fsize(in); R.shaIn = sha_file(in); int T = cores();
    W hx = g_tools + L"hx.exe"; W flags; vector<const Opt *> pres; W gmapProg;
    for (size_t i = 0; i < ST.size(); i++) { if (ST[i].locked || ch[i].on) continue; const Opt &o = ST[i].opts[ch[i].opt];
        if (o.hxflag && wcscmp(o.hxflag, L"gmap")) { if (flags.find(o.hxflag) == W::npos) flags += W(L" ") + o.hxflag; }
        if (o.hxflag && !wcscmp(o.hxflag, L"gmap")) gmapProg = o.pre; else if (o.pre) pres.push_back(&o); }
    if (complete) flags += L" --complete --no-learn";
    // pre-pass order: Leftovers (dedup) first, then Transform; inverse in reverse order
    sort(pres.begin(), pres.end(), [](const Opt *a, const Opt *b) { return wcscmp(a->pre, L"dedup.exe") == 0 && wcscmp(b->pre, L"dedup.exe") != 0; });
    W tf = g_tmp + L"work."; int step = 0;
    auto fail = [&](const W &why) { R.failed = true; R.err = why; };
    for (int r = 0; r < runs && !g_cancel && !R.failed; r++) {
        W pre = L"File " + to_wstring(fileNo) + L" of " + to_wstring(fileCount) + L", run " + to_wstring(r + 1) + L" of " + to_wstring(runs) + L": ";
        double ct = 0, cc = 0, dt = 0, dc = 0; W cur = in; vector<W> chain;
        post_status(pre + L"compressing...");
        for (size_t k = 0; k < pres.size() && !R.failed; k++) { W o = tf + to_wstring(k) + L".pre"; W args = pres[k]->preArgs && *pres[k]->preArgs ? W(L" ") + pres[k]->preArgs : W();
            Run p = run_cmd(q(g_tools + pres[k]->pre) + L" f " + q(cur) + L" " + q(o) + args, false);
            if (!p.ok) { fail(W(pres[k]->pre) + L" forward step failed"); break; } ct += p.sec; cc += p.cpu; R.cmem = max(R.cmem, p.peakMB); chain.push_back(o); cur = o;
            if (adv && r == 0) R.adv += L"    step " + to_wstring(++step) + L": " + pres[k]->pre + L" f" + args + L"  " + human(fsize(o)) + L", " + fmtd(p.sec, 3) + L" s\r\n"; }
        if (R.failed) break;
        if (!gmapProg.empty()) { W map = tf + L"gmap"; Run g = run_cmd(q(g_tools + gmapProg) + L" " + q(cur) + L" " + q(map), false);   // map of the data HX will see
            if (!g.ok) { fail(gmapProg + L" failed"); break; } ct += g.sec; cc += g.cpu; R.cmem = max(R.cmem, g.peakMB);
            if (adv && r == 0) R.adv += L"    step " + to_wstring(++step) + L": " + gmapProg + L" (grouping map) " + fmtd(g.sec, 3) + L" s\r\n"; }
        W hxo = tf + L"hx", gm = gmapProg.empty() ? W() : L" --abl-gmap " + q(tf + L"gmap");
        W cmd = q(hx) + L" c " + q(cur) + L" " + q(hxo) + L" " + to_wstring(T) + flags + gm;
        Run c = run_cmd(cmd, adv && r == 0); if (g_cancel) break; if (!c.ok) { fail(L"HX compress failed (exit " + to_wstring(c.code) + L")"); break; }
        ct += c.sec; cc += c.cpu; R.cmem = max(R.cmem, c.peakMB); R.comp = fsize(hxo);
        if (adv && r == 0) { R.adv += L"    step " + to_wstring(++step) + L": hx c" + flags + (gm.empty() ? W() : L" --abl-gmap MAP") + L"  " + human(R.comp) + L", " + fmtd(c.sec, 3) + L" s\r\n";
            W o = utf16(c.out); size_t p0 = 0; while (p0 < o.size()) { size_t e = o.find(L'\n', p0); W line = o.substr(p0, e == W::npos ? W::npos : e - p0); if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (!line.empty()) R.adv += L"      | " + line + L"\r\n"; if (e == W::npos) break; p0 = e + 1; } }
        post_status(pre + L"decompressing...");
        W dec = tf + L"dec"; Run d = run_cmd(q(hx) + L" d " + q(hxo) + L" " + q(dec) + L" " + to_wstring(T), false);
        if (g_cancel) break; if (!d.ok) { fail(L"HX decompress failed (exit " + to_wstring(d.code) + L")"); break; }
        dt += d.sec; dc += d.cpu; R.dmem = max(R.dmem, d.peakMB); W back = dec;
        for (int k = (int)pres.size() - 1; k >= 0 && !R.failed; k--) { W o = tf + to_wstring(k) + L".inv";
            Run p = run_cmd(q(g_tools + pres[k]->pre) + L" i " + q(back) + L" " + q(o), false);
            if (!p.ok) { fail(W(pres[k]->pre) + L" inverse step failed"); break; } dt += p.sec; dc += p.cpu; R.dmem = max(R.dmem, p.peakMB);
            if (back != dec) DeleteFileW(back.c_str()); back = o; }
        if (R.failed) break;
        post_status(pre + L"checking the round trip...");
        W sh = sha_file(back); if (r == 0) R.shaOut = sh;
        if (sh != R.shaIn || fsize(back) != R.orig) { fail(L"decoded file does NOT match the original"); R.shaOut = sh; break; }
        R.ct.push_back(ct); R.cc.push_back(cc); R.dt.push_back(dt); R.dc.push_back(dc);
        for (auto &x : chain) DeleteFileW(x.c_str()); DeleteFileW(hxo.c_str()); DeleteFileW(dec.c_str()); if (back != dec) DeleteFileW(back.c_str());
    }
    DeleteFileW((tf + L"gmap").c_str());
    R.exact = !R.failed && !R.ct.empty(); return R;
}
static W res_text(const Res &R, int runs) {
    W s = L"File: " + basename(R.file) + L"\r\n  SHA-256 " + R.shaIn + L"\r\n";
    if (R.failed) { s += L"  RESULT: " + R.err + L"\r\n"; return s; }
    if (R.ct.empty()) { s += L"  cancelled\r\n"; return s; }
    double frac = 100.0 * R.comp / max<long long>(1, R.orig);
    s += L"  Size: 100% -> " + pct(frac) + L"    " + human(R.orig) + L" -> " + human(R.comp) + L"\r\n";
    s += L"  Exact bytes: " + fmtn(R.orig) + L" -> " + fmtn(R.comp) + L"\r\n";
    s += L"  Compression ratio: " + fmtd((double)R.orig / max<long long>(1, R.comp), 3) + L"x    bits per byte: " + fmtd(8.0 * R.comp / max<long long>(1, R.orig), 3) + L"\r\n";
    auto tl = [&](const wchar_t *what, const vector<double> &t, const vector<double> &c) { double m = median(t);
        return W(L"  ") + what + fmtd(m, 3) + L" s median (min " + fmtd(*min_element(t.begin(), t.end()), 3) + L", max " + fmtd(*max_element(t.begin(), t.end()), 3) + L"), " +
               fmtd(R.orig / 1e6 / max(1e-9, m), 2) + L" MB/s, CPU time " + (median(c) > 0 ? fmtd(median(c), 3) + L" s" : W(L"not measured")) + L"\r\n"; };
    s += tl(L"Compress:   ", R.ct, R.cc); s += tl(L"Decompress: ", R.dt, R.dc);
    s += L"  Peak memory (committed): compress " + (R.cmem > 0 ? fmtd(R.cmem, 1) + L" MB" : W(L"not measured")) + L", decompress " + (R.dmem > 0 ? fmtd(R.dmem, 1) + L" MB" : W(L"not measured")) + L"\r\n";
    s += L"  Round trip: exact, all " + to_wstring(R.ct.size()) + L" runs (decoded SHA-256 matches the original)\r\n";
    if (!R.adv.empty()) s += L"  Steps (first run):\r\n" + R.adv;
    return s;
}

// ------------------------------------------------------------------ the test run (worker thread)
static void worker(vector<W> files, vector<Choice> ch, bool complete, int runs, bool adv) {
    W log; SYSTEMTIME st; GetLocalTime(&st); wchar_t when[64]; swprintf(when, 64, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    log += W(TITLE) + L"\r\n" + VERSION + L"   " + when + L"\r\n";
    log += L"Computer: " + machine_info() + L"\r\n";
    log += L"HX program: " + g_tools + L"hx.exe, " + fmtn(fsize(g_tools + L"hx.exe")) + L" bytes, SHA-256 " + sha_file(g_tools + L"hx.exe") + L"\r\n";
    log += L"Runs per file: " + to_wstring(runs) + L" (times shown as median, min, max). Threads: " + to_wstring(cores()) + L".\r\n";
    log += plain_settings(ch, complete) + L"Settings code: " + settings_code(ch, complete) + L"\r\n";
    if (adv) { log += L"Programs used:\r\n"; for (size_t i = 0; i < ST.size(); i++) if (!ST[i].locked && !ch[i].on) { const Opt &o = ST[i].opts[ch[i].opt];
        if (o.pre) log += L"  " + W(o.pre) + L"  " + fmtn(fsize(g_tools + o.pre)) + L" bytes, SHA-256 " + sha_file(g_tools + o.pre) + L"\r\n"; } }
    log += L"Every number below is measured on this computer. Times are wall-clock and include program start-up.\r\n\r\n";
    long long to = 0, tc = 0; double tct = 0, tdt = 0; int ok = 0, bad = 0;
    for (size_t i = 0; i < files.size() && !g_cancel; i++) {
        Res R = run_one(files[i], ch, complete, runs, adv, (int)i + 1, (int)files.size());
        log += res_text(R, runs) + L"\r\n";
        if (R.exact) { ok++; to += R.orig; tc += R.comp; tct += median(R.ct); tdt += median(R.dt); } else if (!g_cancel) bad++;
        PostMessageW(g_wnd, WM_STATUS, 1, (LPARAM)new W(log));
    }
    if (files.size() > 1 && ok) {
        log += L"TOTAL for " + to_wstring(ok) + L" files" + (bad ? L" (" + to_wstring(bad) + L" failed, not counted)" : W()) + L"\r\n";
        log += L"  Size: 100% -> " + pct(100.0 * tc / max<long long>(1, to)) + L"    " + human(to) + L" -> " + human(tc) + L"\r\n";
        log += L"  Exact bytes: " + fmtn(to) + L" -> " + fmtn(tc) + L"    ratio " + fmtd((double)to / max<long long>(1, tc), 3) + L"x\r\n";
        log += L"  Sum of median times: compress " + fmtd(tct, 3) + L" s, decompress " + fmtd(tdt, 3) + L" s\r\n"; }
    if (g_cancel) log += L"CANCELLED by the user - results above are only for the runs that finished.\r\n";
    PostMessageW(g_wnd, WM_DONE, 0, (LPARAM)new W(log));
}

// ------------------------------------------------------------------ painting: soft grey texture, shadowed panels
static void paint_bg(HDC dc, RECT rc) {
    int w = rc.right, h = rc.bottom;
    for (int y = 0; y < h; y += 2) { int v = 214 - 14 * y / max(1, h); HBRUSH b = CreateSolidBrush(RGB(v, v, v + 1)); RECT r = {0, y, w, y + 2}; FillRect(dc, &r, b); DeleteObject(b); }
    uint32_t s = 12345; for (int i = 0; i < w * h / 180; i++) { s = s * 1103515245 + 12345; int x = (s >> 8) % max(1, w); s = s * 1103515245 + 12345; int y = (s >> 8) % max(1, h);
        COLORREF c = GetPixel(dc, x, y); int d = (s >> 20) & 1 ? 6 : -6; SetPixel(dc, x, y, RGB(min(255, max(0, (int)GetRValue(c) + d)), min(255, max(0, (int)GetGValue(c) + d)), min(255, max(0, (int)GetBValue(c) + d)))); }
    { HGDIOBJ of = SelectObject(dc, g_titleFont); SetBkMode(dc, TRANSPARENT); RECT t = g_titleRect; OffsetRect(&t, 1, 1); SetTextColor(dc, RGB(245, 245, 245));
      DrawTextW(dc, TITLE, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SetTextColor(dc, RGB(35, 35, 40)); DrawTextW(dc, TITLE, -1, &g_titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of); }
    for (auto &p : g_panels) { if (p.right <= p.left) continue;
        for (int k = S(6); k >= 1; k--) { int v = 150 + 12 * k; HBRUSH b = CreateSolidBrush(RGB(v, v, v)); RECT r = {p.left + k, p.top + k, p.right + k, p.bottom + k}; FillRect(dc, &r, b); DeleteObject(b); }
        FillRect(dc, &p, g_panelBrush); HPEN pen = CreatePen(PS_SOLID, 1, RGB(196, 196, 196)); HGDIOBJ op = SelectObject(dc, pen); HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, p.left, p.top, p.right, p.bottom); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); }
}

// ------------------------------------------------------------------ layout
static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD st, int id, HFONT f = NULL) {
    HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, g_wnd, (HMENU)(intptr_t)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : g_font), TRUE); return h;
}
static void layout() {
    RECT rc; GetClientRect(g_wnd, &rc); int w = rc.right, h = rc.bottom, m = S(14), y = S(10);
    g_titleRect = {m, y, w - m, y + S(26)}; y += S(34);
    // panel 1: stages
    int p1top = y, rowh = S(30), x = m + S(10), iw = w - 2 * m - S(20);
    MoveWindow(g_lbl[0], x, y + S(8), S(70), S(20), TRUE); MoveWindow(g_fast, x + S(70), y + S(6), S(100), S(22), TRUE); MoveWindow(g_complete, x + S(172), y + S(6), S(120), S(22), TRUE);
    MoveWindow(g_hmode, x + S(296), y + S(6), S(24), S(22), TRUE);
    MoveWindow(g_enall, x + iw - S(250), y + S(5), S(120), S(26), TRUE); MoveWindow(g_disall, x + iw - S(124), y + S(5), S(124), S(26), TRUE); y += S(40);
    int cL = S(230), cQ = S(24), cR = S(70), cO = S(62), cH = S(24), gap = S(8); int cC = iw - cL - cQ - cR - cO - cH - 5 * gap; if (cC < S(200)) cC = S(200);
    for (size_t i = 0; i < g_rows.size(); i++) { int xx = x; auto &r = g_rows[i];
        MoveWindow(r.label, xx, y + S(5), cL, S(20), TRUE); xx += cL + gap - S(4); MoveWindow(r.hs, xx, y + S(1), cQ, S(24), TRUE); xx += cQ + gap;
        MoveWindow(r.on, xx, y + S(3), cR, S(22), TRUE); xx += cR + gap; MoveWindow(r.off, xx, y + S(3), cO, S(22), TRUE); xx += cO + gap;
        MoveWindow(r.combo, xx, y, cC, S(300), TRUE); xx += cC + gap; MoveWindow(r.ho, xx, y + S(1), cH, S(24), TRUE); y += rowh; }
    y += S(6); g_panels[0] = {m, p1top, w - m, y};
    // panel 2: file and run options
    y += S(14); int p2top = y; y += S(10);
    MoveWindow(g_load, x, y, S(130), S(28), TRUE); MoveWindow(g_folderchk, x + S(142), y + S(4), S(290), S(22), TRUE); MoveWindow(g_folder, x + S(436), y, S(130), S(28), TRUE);
    MoveWindow(g_file, x + S(576), y + S(6), iw - S(576), S(20), TRUE); y += S(38);
    MoveWindow(g_lbl[1], x, y + S(5), S(44), S(20), TRUE); MoveWindow(g_runs, x + S(46), y, S(60), S(300), TRUE);
    MoveWindow(g_lbl[2], x + S(126), y + S(5), S(32), S(20), TRUE); MoveWindow(g_basic, x + S(160), y + S(3), S(100), S(22), TRUE); MoveWindow(g_adv, x + S(262), y + S(3), S(130), S(22), TRUE);
    MoveWindow(g_run, x + iw - S(380), y - S(1), S(120), S(30), TRUE); MoveWindow(g_cancelb, x + iw - S(252), y - S(1), S(120), S(30), TRUE); MoveWindow(g_save, x + iw - S(124), y - S(1), S(124), S(30), TRUE);
    y += S(40); g_panels[1] = {m, p2top, w - m, y};
    // panel 3: log
    y += S(14); int p3top = y; int sb = S(26);
    MoveWindow(g_log, m + S(8), y + S(8), w - 2 * m - S(16), h - y - sb - S(24), TRUE);
    g_panels[2] = {m, p3top, w - m, h - sb - S(8)};
    MoveWindow(g_status, m, h - sb + S(2), w - 2 * m, S(20), TRUE);
    InvalidateRect(g_wnd, NULL, TRUE);
}
static void set_busy(bool b) { g_busy = b;
    for (HWND hh : {g_enall, g_disall, g_fast, g_complete, g_load, g_folder, g_folderchk, g_runs, g_basic, g_adv, g_run, g_save}) EnableWindow(hh, !b);
    EnableWindow(g_cancelb, b); for (size_t i = 0; i < g_rows.size(); i++) sync_row(i); }
static void show_help(const W &txt) { MessageBoxW(g_wnd, txt.c_str(), L"HX Ablation Test - explanation", MB_OK | MB_ICONINFORMATION); }
static void update_file_label() {
    W t; if (g_useFolder) t = g_folderPath.empty() ? W(L"No folder chosen.") : L"Folder: " + g_folderPath;
    else t = g_path.empty() ? W(L"No file loaded.") : L"File: " + basename(g_path) + L" (" + human(fsize(g_path)) + L")";
    SetWindowTextW(g_file, t.c_str());
}
static vector<W> folder_files(const W &dir) { vector<W> v; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) v.push_back(dir + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd)); FindClose(h); }
    sort(v.begin(), v.end()); return v; }
static void save_log() {
    if (g_logText.empty()) { MessageBoxW(g_wnd, L"There is no log yet. Run a test first.", L"Save log", MB_ICONINFORMATION); return; }
    SYSTEMTIME st; GetLocalTime(&st); wchar_t fn[MAX_PATH]; swprintf(fn, MAX_PATH, L"HX-Ablation-Log-%04d-%02d-%02d-%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    OPENFILENAMEW of = {sizeof of}; of.hwndOwner = g_wnd; of.lpstrFile = fn; of.nMaxFile = MAX_PATH; of.lpstrFilter = L"Text file (*.txt)\0*.txt\0All files\0*.*\0";
    of.lpstrInitialDir = g_dir.c_str(); of.lpstrDefExt = L"txt"; of.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    if (!GetSaveFileNameW(&of)) return;
    FILE *f = _wfopen(fn, L"wb"); if (!f) { MessageBoxW(g_wnd, L"Could not write that file.", L"Save log", MB_ICONERROR); return; }
    fputs("\xEF\xBB\xBF", f); int n = WideCharToMultiByte(CP_UTF8, 0, g_logText.c_str(), -1, NULL, 0, NULL, NULL); string u(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, g_logText.c_str(), -1, &u[0], n, NULL, NULL); fwrite(u.data(), 1, n - 1, f); fclose(f); set_status(W(L"Log saved: ") + fn);
}
static void start_run() {
    vector<W> files;
    if (g_useFolder) { if (g_folderPath.empty()) { MessageBoxW(g_wnd, L"Choose a folder first.", L"No folder", MB_ICONINFORMATION); return; } files = folder_files(g_folderPath);
        if (files.empty()) { MessageBoxW(g_wnd, L"That folder has no files in it.", L"No files", MB_ICONINFORMATION); return; } }
    else { if (g_path.empty() || fsize(g_path) < 0) { MessageBoxW(g_wnd, L"Load a file first.", L"No file", MB_ICONINFORMATION); return; } files.push_back(g_path); }
    if (fsize(g_tools + L"hx.exe") < 0) { MessageBoxW(g_wnd, (L"hx.exe is missing from " + g_tools).c_str(), L"Missing program", MB_ICONERROR); return; }
    vector<Choice> ch(ST.size());
    for (size_t i = 0; i < ST.size(); i++) { ch[i].on = ST[i].locked || SendMessageW(g_rows[i].on, BM_GETCHECK, 0, 0) == BST_CHECKED; ch[i].opt = max(0, (int)SendMessageW(g_rows[i].combo, CB_GETCURSEL, 0, 0));
        if (!ch[i].on && !opt_available(ST[i].opts[ch[i].opt])) { MessageBoxW(g_wnd, (W(L"The replacement \"") + ST[i].opts[ch[i].opt].name + L"\" needs " + ST[i].opts[ch[i].opt].pre + L", which is not in the tools folder yet.").c_str(), L"Program not installed", MB_ICONINFORMATION); return; } }
    g_runsN = (int)SendMessageW(g_runs, CB_GETCURSEL, 0, 0) + 1; g_advanced = SendMessageW(g_adv, BM_GETCHECK, 0, 0) == BST_CHECKED; g_complete_mode = SendMessageW(g_complete, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_cancel = false; set_busy(true); SetWindowTextW(g_log, L""); g_logText.clear(); CreateDirectoryW(g_tmp.c_str(), NULL);
    thread(worker, files, ch, g_complete_mode, g_runsN, g_advanced).detach();
}
static LRESULT CALLBACK wnd(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hw; g_dpi = GetDpiForWindow(hw); g_panelBrush = CreateSolidBrush(PANEL);
        NONCLIENTMETRICSW ncm = {sizeof ncm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont; lf.lfHeight = -S(13); g_font = CreateFontIndirectW(&lf); lf.lfWeight = FW_SEMIBOLD; g_bold = CreateFontIndirectW(&lf);
        lf.lfHeight = -S(17); lf.lfWeight = FW_BOLD; g_titleFont = CreateFontIndirectW(&lf);
        LOGFONTW mf = {}; mf.lfHeight = -S(12); wcscpy(mf.lfFaceName, L"Consolas"); g_mono = CreateFontIndirectW(&mf);
        g_lbl[0] = mk(L"STATIC", L"HX mode:", 0, 0, g_bold);
        g_fast = mk(L"BUTTON", L"HX Fast", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, ID_FAST); g_complete = mk(L"BUTTON", L"HX Complete", BS_AUTORADIOBUTTON, ID_COMPLETE);
        SendMessageW(g_fast, BM_SETCHECK, BST_CHECKED, 0); g_hmode = mk(L"BUTTON", L"?", BS_PUSHBUTTON, ID_HMODE);
        g_enall = mk(L"BUTTON", L"Enable all", BS_PUSHBUTTON | WS_TABSTOP, ID_ENALL); g_disall = mk(L"BUTTON", L"Disable all", BS_PUSHBUTTON | WS_TABSTOP, ID_DISALL);
        for (size_t i = 0; i < ST.size(); i++) { RowW r;
            r.label = mk(L"STATIC", ST[i].label, SS_LEFT, 0, g_bold); r.hs = mk(L"BUTTON", L"?", BS_PUSHBUTTON | WS_TABSTOP, ID_HSTAGE + (int)i);
            r.on = mk(L"BUTTON", L"On", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, ID_RADIO + 2 * (int)i); r.off = mk(L"BUTTON", L"Off", BS_AUTORADIOBUTTON, ID_RADIO + 2 * (int)i + 1);
            r.combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_COMBO + (int)i); r.ho = mk(L"BUTTON", L"?", BS_PUSHBUTTON | WS_TABSTOP, ID_HOPT + (int)i);
            SendMessageW(r.on, BM_SETCHECK, BST_CHECKED, 0); g_rows.push_back(r); fill_combo(i); sync_row(i); }
        g_load = mk(L"BUTTON", L"Load file...", BS_PUSHBUTTON | WS_TABSTOP, ID_LOAD);
        g_folderchk = mk(L"BUTTON", L"Run all files in a folder independently", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FOLDERCHK);
        g_folder = mk(L"BUTTON", L"Choose folder...", BS_PUSHBUTTON | WS_TABSTOP, ID_FOLDER); g_file = mk(L"STATIC", L"No file loaded.", SS_LEFT | SS_PATHELLIPSIS, ID_FILE);
        g_lbl[1] = mk(L"STATIC", L"Runs:", 0, 0, g_bold); g_runs = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_RUNS);
        for (int i = 1; i <= 10; i++) SendMessageW(g_runs, CB_ADDSTRING, 0, (LPARAM)to_wstring(i).c_str()); SendMessageW(g_runs, CB_SETCURSEL, 4, 0);
        g_lbl[2] = mk(L"STATIC", L"Log:", 0, 0, g_bold);
        g_basic = mk(L"BUTTON", L"Basic log", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, ID_BASIC); g_adv = mk(L"BUTTON", L"Advanced log", BS_AUTORADIOBUTTON, ID_ADV); SendMessageW(g_basic, BM_SETCHECK, BST_CHECKED, 0);
        g_run = mk(L"BUTTON", L"Run test", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_RUN); g_cancelb = mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, ID_CANCEL);
        g_save = mk(L"BUTTON", L"Save log...", BS_PUSHBUTTON | WS_TABSTOP, ID_SAVE); EnableWindow(g_cancelb, FALSE);
        g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 10, 10, hw, (HMENU)ID_LOG, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_log, WM_SETFONT, (WPARAM)g_mono, TRUE); SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
        g_status = mk(L"STATIC", L"Choose the stages, load a file (or a folder), then press Run test.", SS_LEFT, ID_STATUS);
        layout(); return 0; }
    case WM_ERASEBKGND: { RECT rc; GetClientRect(hw, &rc); HDC dc = (HDC)wp; HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bm);
        paint_bg(mem, rc); BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem); return 1; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: { HDC dc = (HDC)wp; HWND c = (HWND)lp;
        if (c == g_log) { SetBkColor(dc, RGB(248, 248, 246)); static HBRUSH lb = CreateSolidBrush(RGB(248, 248, 246)); return (LRESULT)lb; }
        if (c == g_status) { static HBRUSH sbr = CreateSolidBrush(RGB(202, 202, 203)); SetBkColor(dc, RGB(202, 202, 203)); SetTextColor(dc, RGB(30, 30, 30)); return (LRESULT)sbr; }
        SetBkColor(dc, PANEL); SetTextColor(dc, RGB(25, 25, 25)); return (LRESULT)g_panelBrush; }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: { auto *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(1000); mm->ptMinTrackSize.y = S(760); return 0; }
    case WM_DPICHANGED: { g_dpi = HIWORD(wp); RECT *r = (RECT *)lp; SetWindowPos(hw, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); return 0; }
    case WM_STATUS: { W *s = (W *)lp; if (wp == 1) { SetWindowTextW(g_log, s->c_str()); SendMessageW(g_log, EM_SETSEL, s->size(), s->size()); SendMessageW(g_log, EM_SCROLLCARET, 0, 0); } else set_status(*s); delete s; return 0; }
    case WM_DONE: { W *s = (W *)lp; g_logText = *s; delete s; SetWindowTextW(g_log, g_logText.c_str()); set_busy(false);
        set_status(g_cancel ? L"Cancelled. Temporary files were removed." : L"Finished. Press Save log... to keep the log as a text file."); return 0; }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= ID_RADIO && id < ID_RADIO + 200) { sync_row((id - ID_RADIO) / 2); return 0; }
        if (id >= ID_HSTAGE && id < ID_HSTAGE + 100) { show_help(ST[id - ID_HSTAGE].stageHelp); return 0; }
        if (id >= ID_HOPT && id < ID_HOPT + 100) { size_t i = id - ID_HOPT; bool on = ST[i].locked || SendMessageW(g_rows[i].on, BM_GETCHECK, 0, 0) == BST_CHECKED;
            int k = max(0, (int)SendMessageW(g_rows[i].combo, CB_GETCURSEL, 0, 0)); show_help(on || ST[i].opts.empty() ? W(ST[i].mineHelp) : W(ST[i].opts[k].help)); return 0; }
        switch (id) {
        case ID_HMODE: show_help(L"HX MODE\r\n\r\nHX Fast: Harrington's Sets, the Exchange lines, the cascade's repeat line and the split route.\r\n\r\nHX Complete: "
            L"everything in HX Fast, plus whole-file runs of the standard compressors BSC (Ilya Grebnov), bzip3 (Kamila Szewczyk), LZMA and PPMd (Igor Pavlov) "
            L"and Brotli (Google). The smallest result is kept. These standard compressors are the same in every run."); break;
        case ID_ENALL: set_all(true); break;
        case ID_DISALL: set_all(false); break;
        case ID_LOAD: { wchar_t fn[4096] = L""; W init = g_dir + L"testfiles"; OPENFILENAMEW of = {sizeof of}; of.hwndOwner = hw; of.lpstrFile = fn; of.nMaxFile = 4096; of.lpstrInitialDir = init.c_str(); of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&of)) { g_path = fn; g_useFolder = false; SendMessageW(g_folderchk, BM_SETCHECK, BST_UNCHECKED, 0); update_file_label(); } break; }
        case ID_FOLDERCHK: g_useFolder = SendMessageW(g_folderchk, BM_GETCHECK, 0, 0) == BST_CHECKED; update_file_label(); break;
        case ID_FOLDER: { BROWSEINFOW bi = {}; bi.hwndOwner = hw; bi.lpszTitle = L"Choose the folder of test files (each file is tested on its own)"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi); if (pid) { wchar_t p[MAX_PATH]; if (SHGetPathFromIDListW(pid, p)) { g_folderPath = p; g_useFolder = true; SendMessageW(g_folderchk, BM_SETCHECK, BST_CHECKED, 0); update_file_label(); } CoTaskMemFree(pid); } break; }
        case ID_RUN: if (!g_busy) start_run(); break;
        case ID_CANCEL: if (g_busy) { g_cancel = true; EnterCriticalSection(&g_jobLock); if (g_curJob) TerminateJobObject(g_curJob, 1); LeaveCriticalSection(&g_jobLock); set_status(L"Cancelling..."); } break;
        case ID_SAVE: save_log(); break;
        }
        (void)code; return 0; }
    case WM_CLOSE: if (g_busy) { if (MessageBoxW(hw, L"A test is running. Stop it and close?", L"HX Ablation Test", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g_cancel = true; EnterCriticalSection(&g_jobLock); if (g_curJob) TerminateJobObject(g_curJob, 1); LeaveCriticalSection(&g_jobLock); } DestroyWindow(hw); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic); InitializeCriticalSection(&g_jobLock);
    wchar_t self[4096]; GetModuleFileNameW(NULL, self, 4096); g_dir = self; g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1);
    g_tools = g_dir + L"tools\\"; CreateDirectoryW((g_dir + L"testfiles").c_str(), NULL);
    wchar_t tp[MAX_PATH]; GetTempPathW(MAX_PATH, tp); g_tmp = W(tp) + L"hx-ablation\\"; CreateDirectoryW(g_tmp.c_str(), NULL);
    build_stages();
    {   // self-test: hx-ablation.exe /selftest FILE REPORT [Fast|Complete] - runs all-on, every single replacement, and all-off
        int ac; wchar_t **av = CommandLineToArgvW(GetCommandLineW(), &ac);
        if (ac >= 4 && !wcscmp(av[1], L"/selftest")) { bool cpl = ac >= 5 && !wcscmp(av[4], L"Complete"); W all;
            vector<vector<Choice>> sets; vector<Choice> on(ST.size(), {true, 0}); sets.push_back(on);
            for (size_t i = 0; i < ST.size(); i++) if (!ST[i].locked) for (size_t k = 0; k < ST[i].opts.size(); k++) if (opt_available(ST[i].opts[k])) { auto c = on; c[i] = {false, (int)k}; sets.push_back(c); }
            vector<Choice> off(ST.size(), {false, 0}); for (size_t i = 0; i < ST.size(); i++) off[i].on = ST[i].locked; sets.push_back(off);
            all += W(TITLE) + L"\r\nSELF-TEST " + VERSION + L"\r\nComputer: " + machine_info() + L"\r\n\r\n";
            for (auto &c : sets) { Res R = run_one(av[2], c, cpl, 1, false, 1, 1); all += plain_settings(c, cpl) + res_text(R, 1) + L"\r\n"; }
            FILE *f = _wfopen(av[3], L"wb"); if (f) { fputs("\xEF\xBB\xBF", f); int n = WideCharToMultiByte(CP_UTF8, 0, all.c_str(), -1, NULL, 0, NULL, NULL); string u(n, 0);
                WideCharToMultiByte(CP_UTF8, 0, all.c_str(), -1, &u[0], n, NULL, NULL); fwrite(u.data(), 1, n - 1, f); fclose(f); }
            return 0; }
        LocalFree(av);
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = wnd; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = NULL;
    wc.lpszClassName = L"HXAblation"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(2)); RegisterClassW(&wc);
    int dpi = GetDpiForSystem();
    HWND w = CreateWindowExW(0, L"HXAblation", L"HX Ablation Test - Patent Pending", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1120, dpi, 96), MulDiv(900, dpi, 96), NULL, NULL, hi, NULL);
    ShowWindow(w, show); MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) { if (!IsDialogMessageW(w, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } } return 0;
}
