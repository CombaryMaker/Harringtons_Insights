// h2h.cpp - Harrington Head-to-Head: one of Michael's open-source tools against its fixed, same-tier opponent.
// Michael Hugh Harrington, November 2026. MIT License.
//
// Fairness rules built into the program (not in an editable file):
//  - every matchup, opponent, level and thread count is fixed below; the user cannot pair tools freely;
//  - both tools always run on the same file, on the same machine, one after the other (never at the same time);
//  - each compress and decompress is timed separately, repeated, and the median is reported;
//  - the user chooses which side runs first (or alternate every repeat, the default);
//  - every result is decompressed and compared byte for byte with the original before it counts.
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace std;
typedef wstring W;

// ---------------------------------------------------------------- the fixed catalogue
struct Tool { const wchar_t *name, *comp, *decomp; bool cOut, dOut; };   // cOut/dOut: program writes to stdout
// placeholders: {t}=tools folder, {i}=input, {o}=output, {n}=thread count
static const Tool T_HSZ   = {L"hs_zstd 1.3 (zstd level 3, Harrington Sets)", L"\"{t}hs_zstd.exe\" c \"{i}\" \"{o}\" --level 3 --jobs {n}", L"\"{t}hs_zstd.exe\" d \"{i}\" \"{o}\" --jobs {n}", false, false};
static const Tool T_ZST3  = {L"zstd 1.5.6 -3 -T{n} (stock)", L"\"{t}zstd.exe\" -q -f -3 -T{n} \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false};
static const Tool T_HCM   = {L"HCM v3.6.1 (fast mode)", L"\"{t}HCM.exe\" c \"{i}\" \"{o}\"", L"\"{t}HCM.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_ZPQ5  = {L"ZPAQ 7.15 -m5 (stock libzpaq)", L"\"{t}zpaq-stock.exe\" c 5 \"{i}\" \"{o}\"", L"\"{t}zpaq-stock.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_LPAQ  = {L"lpaq1 -6 (stock, Matt Mahoney)", L"\"{t}lpaq1.exe\" 6 \"{i}\" \"{o}\"", L"\"{t}lpaq1.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_ZPQHI = {L"zpaq-hi -m5", L"\"{t}zpaq-hi.exe\" c 5 \"{i}\" \"{o}\"", L"\"{t}zpaq-stock.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_LPHI  = {L"lpaq1-h -6 (best of 6 settings)", L"\"{t}lpaq1-h-best.exe\" 6 \"{i}\" \"{o}\"", L"\"{t}lpaq1-h.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_BZHI  = {L"bzip2-hi", L"\"{t}bzip2-hi.exe\" c \"{i}\" \"{o}\"", L"\"{t}bzip2-stock.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_BZ9   = {L"bzip2 -9 (stock libbz2)", L"\"{t}bzip2-stock.exe\" c \"{i}\" \"{o}\"", L"\"{t}bzip2-stock.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_LZHI  = {L"lzav-hi (LZAV format 4)", L"\"{t}lzav-hi.exe\" c \"{i}\" \"{o}\"", L"\"{t}lzav-hi.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_LZHS  = {L"LZAV 7.1 HI (stock encoder)", L"\"{t}lzav-hi.exe\" s \"{i}\" \"{o}\"", L"\"{t}lzav-hi.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_XZHI  = {L"xz-hi (standard .xz)", L"\"{t}xz-hi.exe\" \"{i}\" \"{o}\"", L"\"{t}xz.exe\" -d -c \"{i}\"", false, true};
static const Tool T_XZ9E  = {L"xz 5.6.3 -9e (stock)", L"\"{t}xz.exe\" -9e -k -c \"{i}\"", L"\"{t}xz.exe\" -d -c \"{i}\"", true, true};
static const Tool T_RDZ   = {L"rd-zstd (record delta + zstd -19)", L"\"{t}rd-zstd.exe\" c \"{i}\" \"{o}\"", L"\"{t}rd-zstd.exe\" d \"{i}\" \"{o}\"", false, false};
static const Tool T_ZS19  = {L"zstd 1.5.6 -19 (stock)", L"\"{t}zstd.exe\" -q -f -19 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false};

struct TestFile { const wchar_t *type, *file; long long size; const wchar_t *corpus, *url; };
static const wchar_t *U_CANT = L"https://corpus.canterbury.ac.nz/descriptions/#cantrbry";
static const wchar_t *U_SIL  = L"https://sun.aei.polsl.pl//~sdeor/index.php?page=silesia";
static const wchar_t *U_ENW  = L"https://mattmahoney.net/dc/textdata.html";
static const TestFile F_ALICE = {L"English text", L"alice29.txt", 152089, L"Canterbury corpus", U_CANT};
static const TestFile F_LCET  = {L"English text (technical)", L"lcet10.txt", 426754, L"Canterbury corpus", U_CANT};
static const TestFile F_PLRA  = {L"English text (poetry)", L"plrabn12.txt", 481861, L"Canterbury corpus", U_CANT};
static const TestFile F_KENN  = {L"Spreadsheet records", L"kennedy.xls", 1029744, L"Canterbury corpus", U_CANT};
static const TestFile F_PTT5  = {L"Fax image (1-bit rows)", L"ptt5", 513216, L"Canterbury corpus", U_CANT};
static const TestFile F_SUM   = {L"SPARC executable", L"sum", 38240, L"Canterbury corpus", U_CANT};
static const TestFile F_DICK  = {L"English books (large)", L"dickens", 10192446, L"Silesia corpus", U_SIL};
static const TestFile F_MOZ   = {L"Program tarball (large)", L"mozilla", 51220480, L"Silesia corpus", U_SIL};
static const TestFile F_SAMBA = {L"Source code tarball", L"samba", 21606400, L"Silesia corpus", U_SIL};
static const TestFile F_XRAY  = {L"Medical x-ray image (12-bit)", L"x-ray", 8474240, L"Silesia corpus", U_SIL};
static const TestFile F_MR    = {L"Medical MR image (16-bit)", L"mr", 9970564, L"Silesia corpus", U_SIL};
static const TestFile F_OOFF  = {L"Office program (x86)", L"ooffice", 6152192, L"Silesia corpus", U_SIL};
static const TestFile F_NCI   = {L"Chemical database (text records)", L"nci", 33553445, L"Silesia corpus", U_SIL};
static const TestFile F_ENW8  = {L"Wikipedia XML (100 MB)", L"enwik8", 100000000, L"Large Text Compression Benchmark", U_ENW};

struct Matchup {
    const Tool *mine; vector<const Tool *> opp; bool threads;
    vector<const TestFile *> files; const wchar_t *note;
};
static vector<Matchup> M;
static void build_catalogue() {
    M = {
        {&T_HSZ, {&T_ZST3}, true, {&F_ENW8, &F_MOZ, &F_NCI, &F_SAMBA},
         L"Paired by speed tier: both zstd level 3, same thread count. hs_zstd decodes on every core; the stock zstd decoder is "
         L"single-threaded by design, which is the difference hs_zstd was built for. It is meant for large files (64 MB and up); on "
         L"small files the two are about the same."},
        {&T_HCM, {&T_ZPQ5, &T_LPAQ}, false, {&F_ALICE, &F_LCET, &F_PLRA, &F_KENN, &F_PTT5, &F_DICK},
         L"Context-mixing tier (slow and strong): ZPAQ -m5 and lpaq1 are HCM's speed neighbours. ZPAQ is also a full archiver "
         L"(deduplication, versions, many files); here only its single-stream compression is compared. HCM runs single-threaded here."},
        {&T_ZPQHI, {&T_ZPQ5}, false, {&F_KENN, &F_PTT5, &F_LCET, &F_PLRA},
         L"Same program, same level (-m5). zpaq-hi tries up to four models per block and keeps the smallest, so it compresses about "
         L"1.8x slower; where an extra model wins, decoding is slower too. The stock ZPAQ decoder reads both outputs."},
        {&T_LPHI, {&T_LPAQ}, false, {&F_KENN, &F_PTT5, &F_SUM, &F_ALICE},
         L"Same engine and memory (option 6). lpaq1-h tries six settings per file, so it compresses about 6x slower. Files that use "
         L"its record additions need the lpaq1-h decoder; on plain text it writes the same file as stock lpaq1."},
        {&T_BZHI, {&T_BZ9}, false, {&F_ALICE, &F_DICK, &F_KENN, &F_SAMBA},
         L"Both are standard .bz2 read by the stock libbz2 decoder. bzip2-hi also chooses the block size (1-9) and searches the "
         L"Huffman tables, so it compresses several times slower; stock bzip2 runs at -9."},
        {&T_LZHI, {&T_LZHS}, false, {&F_DICK, &F_SAMBA, &F_MOZ, &F_NCI},
         L"Same LZAV format, both decoded by the stock LZAV 7.1 decoder. lzav-hi uses optimal parsing, so it compresses far slower; "
         L"it is built for files written once and read many times."},
        {&T_XZHI, {&T_XZ9E}, false, {&F_KENN, &F_SUM, &F_OOFF, &F_XRAY, &F_NCI},
         L"Both are standard .xz decoded by stock xz. xz-hi searches xz's own settings and filters per file (up to about 400 tries "
         L"on binary data), so it compresses far slower; stock xz runs at -9e."},
        {&T_RDZ, {&T_ZS19}, false, {&F_XRAY, &F_MR, &F_KENN, &F_PTT5},
         L"Both zstd level 19. rd-zstd adds a record-length delta for fixed-record data (tables, images, sensor rows). On other data "
         L"it keeps plain zstd and adds an 8-byte header, so it can be 8 bytes larger."},
    };
}
static const wchar_t *GENERAL =
    L"These test files are the data types each Harrington tool was designed for. Both compressors always run on the same file, "
    L"one after the other, on this computer. Matchups, levels and thread counts are fixed in the program. Test files come from "
    L"public corpora; their size and SHA-256 are shown so anyone can check them.";

// ---------------------------------------------------------------- small helpers
static W g_dir, g_tools, g_test, g_tmp;
static W repl(W s, const W &a, const W &b) { size_t p = 0; while ((p = s.find(a, p)) != W::npos) { s.replace(p, a.size(), b); p += b.size(); } return s; }
static W fmtn(long long v) { W s = to_wstring(v < 0 ? -v : v); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L","); return v < 0 ? L"-" + s : s; }
static W fmtd(double v, int dp) { wchar_t b[64]; swprintf(b, 64, L"%.*f", dp, v); return b; }
static long long fsize(const W &f) { WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &a)) return -1; return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow; }
static int cores() { DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS); return n ? (int)n : 1; }
static W basename(const W &p) { size_t k = p.find_last_of(L"\\/"); return k == W::npos ? p : p.substr(k + 1); }

// SHA-256 (FIPS 180-4), for showing which public file was tested
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
    if (h == INVALID_HANDLE_VALUE) return L"?"; Sha s; vector<uint8_t> b(1 << 20); DWORD got;
    while (ReadFile(h, b.data(), (DWORD)b.size(), &got, NULL) && got) s.add(b.data(), got); CloseHandle(h); return s.hex(); }
static bool same_files(const W &a, const W &b) {
    if (fsize(a) != fsize(b)) return false;
    HANDLE x = CreateFileW(a.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    HANDLE y = CreateFileW(b.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    bool ok = x != INVALID_HANDLE_VALUE && y != INVALID_HANDLE_VALUE; vector<char> p(1 << 20), q(1 << 20);
    while (ok) { DWORD m, n; if (!ReadFile(x, p.data(), (DWORD)p.size(), &m, NULL) || !ReadFile(y, q.data(), (DWORD)q.size(), &n, NULL) || m != n) { ok = false; break; }
        if (!m) break; if (memcmp(p.data(), q.data(), m)) ok = false; }
    if (x != INVALID_HANDLE_VALUE) CloseHandle(x); if (y != INVALID_HANDLE_VALUE) CloseHandle(y); return ok;
}

// run one command; time it; record peak memory. stdoutFile non-empty = the program's output goes there.
struct Run { bool ok = false; double sec = 0; double peakMB = 0; DWORD code = 0; };
static Run run_cmd(const W &cmdline, const W &stdoutFile) {
    Run r; SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE out = nul;
    if (!stdoutFile.empty()) out = CreateFileW(stdoutFile.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = {sizeof si}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = out; si.hStdError = nul;
    PROCESS_INFORMATION pi; vector<wchar_t> cl(cmdline.begin(), cmdline.end()); cl.push_back(0);
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    HANDLE job = CreateJobObjectW(NULL, NULL);   // the job records the peak memory of the program and any helper it starts
    if (CreateProcessW(NULL, cl.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, g_tmp.c_str(), &si, &pi)) {
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        QueryPerformanceCounter(&t0); ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE); QueryPerformanceCounter(&t1);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl = {};
        if (job && QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &jl, sizeof jl, NULL)) r.peakMB = jl.PeakJobMemoryUsed / 1048576.0;
        if (r.peakMB <= 0) { PROCESS_MEMORY_COUNTERS pm = {sizeof pm}; if (GetProcessMemoryInfo(pi.hProcess, &pm, sizeof pm)) r.peakMB = pm.PeakPagefileUsage / 1048576.0; }
        GetExitCodeProcess(pi.hProcess, &r.code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        r.sec = double(t1.QuadPart - t0.QuadPart) / f.QuadPart; r.ok = r.code == 0;
    }
    if (job) CloseHandle(job); if (out != nul && out != INVALID_HANDLE_VALUE) CloseHandle(out); CloseHandle(nul); return r;
}
static W expand(const W &tmpl, const W &in, const W &out, int n) { return repl(repl(repl(repl(tmpl, L"{t}", g_tools), L"{i}", in), L"{o}", out), L"{n}", to_wstring(n)); }
static W tool_label(const Tool *t, int n) { return repl(t->name, L"{n}", to_wstring(n)); }

// ---------------------------------------------------------------- window
enum { ID_MINE = 101, ID_OPP, ID_FILE, ID_OWN, ID_WEB, ID_REPS, ID_ORDER, ID_RUN, ID_COPY, ID_LIST, ID_NOTE, ID_INFO, ID_STATUS, ID_GEN };
#define WM_STATUS (WM_APP + 1)
#define WM_DONE (WM_APP + 2)
static HWND g_wnd, g_mine, g_opp, g_file, g_own, g_web, g_reps, g_order, g_run, g_copy, g_list, g_note, g_info, g_status, g_gen;
static HFONT g_font, g_bold; static int g_dpi = 96; static int S(int v) { return MulDiv(v, g_dpi, 96); }
static W g_path; static bool g_busy = false; static W g_report; static W g_sha, g_orderText;
struct Side { W name; long long size = -1; vector<double> ct, dt; double cmem = 0, dmem = 0; bool exact = true, failed = false; W err; };
static Side g_res[2]; static long long g_orig = 0; static int g_mi = 0;

static const Matchup &cur() { int i = (int)SendMessageW(g_mine, CB_GETCURSEL, 0, 0); return M[i < 0 ? 0 : i]; }
static void set_status(const W &s) { SetWindowTextW(g_status, s.c_str()); }
static void show_file_info() {
    g_sha.clear(); if (g_path.empty()) { SetWindowTextW(g_info, L"No test file loaded."); return; }
    long long s = fsize(g_path);
    if (s < 0) { SetWindowTextW(g_info, (L"Missing: " + g_path).c_str()); return; }
    W t = L"File: " + basename(g_path) + L"   " + fmtn(s) + L" bytes   SHA-256: (computing...)"; SetWindowTextW(g_info, t.c_str());
    W p = g_path; thread([p, s]() { W h = sha_file(p); W *m = new W(L"#" + h + L"|" + L"File: " + basename(p) + L"   " + fmtn(s) + L" bytes   SHA-256: " + h); PostMessageW(g_wnd, WM_STATUS, 1, (LPARAM)m); }).detach();
}
static void choose_catalogue_file() {
    int fi = (int)SendMessageW(g_file, CB_GETCURSEL, 0, 0); const Matchup &m = cur();
    if (fi < 0 || fi >= (int)m.files.size()) return;
    const TestFile *f = m.files[fi]; g_path = g_test + f->file; long long s = fsize(g_path);
    if (s < 0) { g_path.clear();
        W msg = W(L"\"") + f->file + L"\" is not in the testfiles folder yet.\n\nIt comes from the " + f->corpus + L" (public).\nDownload it from:\n" + f->url +
                L"\n\nand put it in:\n" + g_test + L"\n\nExpected size: " + fmtn(f->size) + L" bytes.";
        MessageBoxW(g_wnd, msg.c_str(), L"Test file needed", MB_ICONINFORMATION); }
    else if (s != f->size) {
        W msg = W(L"\"") + f->file + L"\" is " + fmtn(s) + L" bytes, but the public file is " + fmtn(f->size) + L" bytes.\nIt may not be the standard corpus file; results will be marked as such.";
        MessageBoxW(g_wnd, msg.c_str(), L"Size does not match", MB_ICONWARNING); }
    show_file_info();
}
static void fill_for_matchup() {
    const Matchup &m = cur(); int n = cores();
    SendMessageW(g_opp, CB_RESETCONTENT, 0, 0); for (auto *o : m.opp) SendMessageW(g_opp, CB_ADDSTRING, 0, (LPARAM)tool_label(o, n).c_str()); SendMessageW(g_opp, CB_SETCURSEL, 0, 0);
    SendMessageW(g_file, CB_RESETCONTENT, 0, 0);
    for (auto *f : m.files) { W s = W(f->type) + L" - " + f->file + L" (" + f->corpus + L")"; SendMessageW(g_file, CB_ADDSTRING, 0, (LPARAM)s.c_str()); }
    W note = W(L"About this matchup: ") + m.note + (m.threads ? L"  Threads: " + to_wstring(n) + L" on both sides." : W(L"  Both sides run single-threaded."));
    SetWindowTextW(g_note, note.c_str()); g_path.clear(); show_file_info(); ListView_DeleteAllItems(g_list); g_report.clear();
}
static vector<vector<W>> g_rows;
static void lv_row(int i, const W &a, const W &b, const W &c, const W &d) {
    g_rows.push_back({a, b, c, d}); if (!g_list) return;
    LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)a.c_str(); ListView_InsertItem(g_list, &it);
    ListView_SetItemText(g_list, i, 1, (LPWSTR)b.c_str()); ListView_SetItemText(g_list, i, 2, (LPWSTR)c.c_str()); ListView_SetItemText(g_list, i, 3, (LPWSTR)d.c_str());
}
static double median(vector<double> v) { if (v.empty()) return 0; sort(v.begin(), v.end()); size_t k = v.size(); return k % 2 ? v[k/2] : (v[k/2-1] + v[k/2]) / 2; }

static void worker(int mi, int oi, W in, int reps, int order) {
    const Matchup &m = M[mi]; const Tool *tl[2] = {m.mine, m.opp[oi]}; int n = m.threads ? cores() : 1;
    g_orig = fsize(in);
    for (int s = 0; s < 2; s++) { g_res[s] = Side(); g_res[s].name = tool_label(tl[s], n); }
    CreateDirectoryW(g_tmp.c_str(), NULL);
    for (int r = 0; r < reps; r++) {
        int first = order == 1 ? 0 : order == 2 ? 1 : (r % 2);   // 0 alternate (mine first on even repeats), 1 mine first, 2 opponent first
        for (int k = 0; k < 2; k++) {
            int s = k == 0 ? first : 1 - first; Side &R = g_res[s]; if (R.failed) continue; const Tool *t = tl[s];
            W cf = g_tmp + (s ? L"opp.cmp" : L"mine.cmp"), df = g_tmp + (s ? L"opp.out" : L"mine.out");
            DeleteFileW(cf.c_str()); DeleteFileW(df.c_str());
            W *st = new W(L"Repeat " + to_wstring(r + 1) + L"/" + to_wstring(reps) + L": compressing with " + R.name + L"..."); PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)st);
            Run c = run_cmd(expand(t->comp, in, cf, n), t->cOut ? cf : W());
            if (!c.ok || fsize(cf) < 0) { R.failed = true; R.err = L"compress failed (exit " + to_wstring(c.code) + L")"; continue; }
            st = new W(L"Repeat " + to_wstring(r + 1) + L"/" + to_wstring(reps) + L": decompressing with " + R.name + L"..."); PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)st);
            Run d = run_cmd(expand(t->decomp, cf, df, n), t->dOut ? df : W());
            if (!d.ok) { R.failed = true; R.err = L"decompress failed (exit " + to_wstring(d.code) + L")"; continue; }
            if (!same_files(in, df)) { R.exact = false; R.failed = true; R.err = L"decoded file does NOT match the original"; continue; }
            R.size = fsize(cf); R.ct.push_back(c.sec); R.dt.push_back(d.sec); R.cmem = max(R.cmem, c.peakMB); R.dmem = max(R.dmem, d.peakMB);
            DeleteFileW(cf.c_str()); DeleteFileW(df.c_str());
        }
    }
    PostMessageW(g_wnd, WM_DONE, 0, 0);
}
static void show_results() {
    if (g_list) ListView_DeleteAllItems(g_list); g_rows.clear(); const Side &a = g_res[0], &b = g_res[1]; int i = 0;
    auto better = [](bool ok, double x, double y, bool lowWins) -> W { if (!ok) return L""; if (x == y) return L"tie"; return (lowWins ? x < y : x > y) ? L"<< Harrington" : L"Opponent >>"; };
    bool ok = !a.failed && !b.failed;
    lv_row(i++, L"Tool", a.name, b.name, L"");
    lv_row(i++, L"Round trip (byte for byte)", a.failed ? a.err : L"exact", b.failed ? b.err : L"exact", L"");
    if (!a.failed || !b.failed) {
        auto sz = [](const Side &s) { return s.failed ? W(L"-") : fmtn(s.size); };
        auto ra = [](const Side &s) { return s.failed ? W(L"-") : fmtd((double)g_orig / max<long long>(1, s.size), 3) + L"x"; };
        lv_row(i++, L"Original size (bytes)", fmtn(g_orig), fmtn(g_orig), L"");
        lv_row(i++, L"Compressed size (bytes)", sz(a), sz(b), better(ok, (double)a.size, (double)b.size, true));
        lv_row(i++, L"Compression ratio", ra(a), ra(b), better(ok, (double)a.size, (double)b.size, true));
        if (ok) { double d = 100.0 * (b.size - a.size) / b.size; lv_row(i++, L"Size difference", d >= 0 ? fmtd(d, 2) + L"% smaller" : fmtd(-d, 2) + L"% larger", L"", L""); }
        auto tm = [](const vector<double> &v) { return v.empty() ? W(L"-") : fmtd(median(v), 3) + L" s"; };
        double ac = median(a.ct), bc = median(b.ct), ad = median(a.dt), bd = median(b.dt);
        lv_row(i++, L"Compress time (median)", tm(a.ct), tm(b.ct), better(ok, ac, bc, true));
        lv_row(i++, L"Decompress time (median)", tm(a.dt), tm(b.dt), better(ok, ad, bd, true));
        lv_row(i++, L"Decompress speed (MB/s)", a.failed ? L"-" : fmtd(g_orig / 1048576.0 / max(1e-9, ad), 1), b.failed ? L"-" : fmtd(g_orig / 1048576.0 / max(1e-9, bd), 1), better(ok, bd, ad, false));
        bool mk = a.cmem > 0 && b.cmem > 0, md = a.dmem > 0 && b.dmem > 0;
        lv_row(i++, L"Peak memory, compress (MB committed)", mk ? fmtd(a.cmem, 1) : W(L"n/a"), mk ? fmtd(b.cmem, 1) : W(L"n/a"), mk ? better(ok, a.cmem, b.cmem, true) : W());
        lv_row(i++, L"Peak memory, decompress (MB committed)", md ? fmtd(a.dmem, 1) : W(L"n/a"), md ? fmtd(b.dmem, 1) : W(L"n/a"), md ? better(ok, a.dmem, b.dmem, true) : W());
        lv_row(i++, L"Timed runs", to_wstring(a.ct.size()), to_wstring(b.ct.size()), L"");
    }
    // plain-text report for copying
    const Matchup &m = M[g_mi]; W r = L"Harrington Head-to-Head 1.0 - " + a.name + L" vs " + b.name + L"\r\n";
    r += L"File: " + basename(g_path) + L" (" + fmtn(g_orig) + L" bytes)" + (g_sha.empty() ? W() : L", SHA-256 " + g_sha) + L"\r\n";
    for (size_t j = 1; j < g_rows.size(); j++) r += g_rows[j][0] + L": " + g_rows[j][1] + L" | " + g_rows[j][2] + (g_rows[j][3].empty() ? W() : L"  [" + g_rows[j][3] + L"]") + L"\r\n";
    r += L"Order: " + g_orderText + L". Each compress and decompress timed separately (wall clock, including program start), median shown.\r\n";
    r += W(L"Matchup note: ") + m.note + L"\r\n" + GENERAL + L"\r\n";
    g_report = r;
}
static void copy_report() {
    if (g_report.empty()) return; if (!OpenClipboard(g_wnd)) return; EmptyClipboard();
    size_t bytes = (g_report.size() + 1) * sizeof(wchar_t); HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) { memcpy(GlobalLock(h), g_report.c_str(), bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard(); set_status(L"Results copied to the clipboard.");
}
static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD st, int id) {
    HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, g_wnd, (HMENU)(intptr_t)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); return h;
}
static HWND g_lbl[6];
static void layout() {
    RECT rc; GetClientRect(g_wnd, &rc); int w = rc.right, h = rc.bottom, m = S(12), y = m, half = (w - 3 * m) / 2;
    MoveWindow(g_gen, m, y, w - 2 * m, S(48), TRUE); y += S(54);
    MoveWindow(g_lbl[0], m, y, half, S(18), TRUE); MoveWindow(g_lbl[1], 2 * m + half, y, half, S(18), TRUE); y += S(20);
    MoveWindow(g_mine, m, y, half, S(300), TRUE); MoveWindow(g_opp, 2 * m + half, y, half, S(300), TRUE); y += S(32);
    MoveWindow(g_lbl[2], m, y, w - 2 * m, S(18), TRUE); y += S(20);
    int bw = S(130); MoveWindow(g_file, m, y, w - 3 * m - 2 * bw - m, S(300), TRUE);
    MoveWindow(g_own, w - 2 * m - 2 * bw, y - S(1), bw, S(26), TRUE); MoveWindow(g_web, w - m - bw, y - S(1), bw, S(26), TRUE); y += S(30);
    MoveWindow(g_info, m, y, w - 2 * m, S(18), TRUE); y += S(26);
    MoveWindow(g_lbl[3], m, y + S(4), S(60), S(18), TRUE); MoveWindow(g_reps, m + S(62), y, S(60), S(200), TRUE);
    MoveWindow(g_lbl[4], m + S(140), y + S(4), S(70), S(18), TRUE); MoveWindow(g_order, m + S(212), y, S(250), S(200), TRUE);
    MoveWindow(g_run, w - 2 * m - 2 * bw, y - S(1), bw, S(28), TRUE); MoveWindow(g_copy, w - m - bw, y - S(1), bw, S(28), TRUE); y += S(36);
    MoveWindow(g_note, m, y, w - 2 * m, S(62), TRUE); y += S(68);
    int lh = h - y - S(34); MoveWindow(g_list, m, y, w - 2 * m, lh, TRUE);
    int cw = (w - 2 * m - S(240) - S(150) - S(24)) / 2; ListView_SetColumnWidth(g_list, 0, S(240)); ListView_SetColumnWidth(g_list, 1, cw); ListView_SetColumnWidth(g_list, 2, cw); ListView_SetColumnWidth(g_list, 3, S(150));
    MoveWindow(g_status, m, h - S(26), w - 2 * m, S(18), TRUE);
}
static void set_busy(bool b) { g_busy = b; for (HWND h : {g_mine, g_opp, g_file, g_own, g_web, g_reps, g_order, g_run, g_copy}) EnableWindow(h, !b); }
static LRESULT CALLBACK wnd(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hw; g_dpi = GetDpiForWindow(hw);
        NONCLIENTMETRICSW ncm = {sizeof ncm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        ncm.lfMessageFont.lfHeight = -S(13); g_font = CreateFontIndirectW(&ncm.lfMessageFont); ncm.lfMessageFont.lfWeight = FW_BOLD; g_bold = CreateFontIndirectW(&ncm.lfMessageFont);
        g_gen = mk(L"STATIC", GENERAL, SS_LEFT, ID_GEN);
        g_lbl[0] = mk(L"STATIC", L"Harrington tool (left side)", 0, 0); g_lbl[1] = mk(L"STATIC", L"Opponent (fixed for this matchup)", 0, 0);
        SendMessageW(g_lbl[0], WM_SETFONT, (WPARAM)g_bold, TRUE); SendMessageW(g_lbl[1], WM_SETFONT, (WPARAM)g_bold, TRUE);
        g_mine = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_MINE); g_opp = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_OPP);
        g_lbl[2] = mk(L"STATIC", L"Test file - the data types this Harrington tool was designed for (or load your own)", 0, 0); SendMessageW(g_lbl[2], WM_SETFONT, (WPARAM)g_bold, TRUE);
        g_file = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_FILE);
        g_own = mk(L"BUTTON", L"Your own file...", BS_PUSHBUTTON | WS_TABSTOP, ID_OWN); g_web = mk(L"BUTTON", L"Where to get it", BS_PUSHBUTTON | WS_TABSTOP, ID_WEB);
        g_info = mk(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS, ID_INFO);
        g_lbl[3] = mk(L"STATIC", L"Repeats:", 0, 0); g_reps = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, ID_REPS);
        for (auto s : {L"1", L"3", L"5", L"7"}) SendMessageW(g_reps, CB_ADDSTRING, 0, (LPARAM)s); SendMessageW(g_reps, CB_SETCURSEL, 1, 0);
        g_lbl[4] = mk(L"STATIC", L"Run first:", 0, 0); g_order = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, ID_ORDER);
        for (auto s : {L"Alternate every repeat (fairest)", L"Harrington tool first", L"Opponent first"}) SendMessageW(g_order, CB_ADDSTRING, 0, (LPARAM)s); SendMessageW(g_order, CB_SETCURSEL, 0, 0);
        g_run = mk(L"BUTTON", L"Run head-to-head", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_RUN); g_copy = mk(L"BUTTON", L"Copy results", BS_PUSHBUTTON | WS_TABSTOP, ID_COPY);
        g_note = mk(L"STATIC", L"", SS_LEFT | WS_BORDER, ID_NOTE);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOSORTHEADER, 0, 0, 10, 10, hw, (HMENU)ID_LIST, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE); ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        const wchar_t *cols[4] = {L"Measure", L"Harrington tool", L"Opponent", L"Better"};
        for (int i = 0; i < 4; i++) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = 100; ListView_InsertColumn(g_list, i, &c); }
        g_status = mk(L"STATIC", L"Choose a matchup and a test file, then press Run.", 0, ID_STATUS);
        for (auto &mt : M) SendMessageW(g_mine, CB_ADDSTRING, 0, (LPARAM)mt.mine->name); SendMessageW(g_mine, CB_SETCURSEL, 0, 0);
        fill_for_matchup(); layout(); return 0; }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: { auto *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(860); mm->ptMinTrackSize.y = S(620); return 0; }
    case WM_DPICHANGED: { g_dpi = HIWORD(wp); RECT *r = (RECT *)lp; SetWindowPos(hw, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); return 0; }
    case WM_STATUS: { W *s = (W *)lp; if (wp == 1) { size_t k = s->find(L'|'); g_sha = s->substr(1, k - 1); SetWindowTextW(g_info, s->substr(k + 1).c_str()); } else set_status(*s); delete s; return 0; }
    case WM_DONE: show_results(); set_busy(false);
        set_status(g_res[0].failed || g_res[1].failed ? L"Finished with a problem - see the round-trip row." : L"Finished. Both results decoded back exactly. Press Copy results to share them.");
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_MINE && code == CBN_SELCHANGE) fill_for_matchup();
        else if (id == ID_FILE && code == CBN_SELCHANGE) choose_catalogue_file();
        else if (id == ID_OWN) { wchar_t fn[4096] = L""; OPENFILENAMEW of = {sizeof of}; of.hwndOwner = hw; of.lpstrFile = fn; of.nMaxFile = 4096; of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&of)) { g_path = fn; SendMessageW(g_file, CB_SETCURSEL, (WPARAM)-1, 0); show_file_info(); } }
        else if (id == ID_WEB) { const Matchup &m = cur(); int fi = (int)SendMessageW(g_file, CB_GETCURSEL, 0, 0);
            const wchar_t *u = fi >= 0 && fi < (int)m.files.size() ? m.files[fi]->url : U_CANT; ShellExecuteW(hw, L"open", u, NULL, NULL, SW_SHOWNORMAL);
            ShellExecuteW(hw, L"open", g_test.c_str(), NULL, NULL, SW_SHOWNORMAL); }
        else if (id == ID_RUN && !g_busy) {
            if (g_path.empty() || fsize(g_path) < 0) { MessageBoxW(hw, L"Choose a test file first (from the list, or Your own file...).", L"No file", MB_ICONINFORMATION); return 0; }
            int mi = (int)SendMessageW(g_mine, CB_GETCURSEL, 0, 0), oi = (int)SendMessageW(g_opp, CB_GETCURSEL, 0, 0);
            const Matchup &m = M[mi]; const Tool *need[2] = {m.mine, m.opp[oi]}; W missing;
            for (auto *t : need) for (const W &tmpl : {W(t->comp), W(t->decomp)}) { size_t a = tmpl.find(L"{t}"), b = tmpl.find(L".exe", a);
                W exe = g_tools + tmpl.substr(a + 3, b + 4 - a - 3); if (fsize(exe) < 0 && missing.find(exe) == W::npos) missing += exe + L"\n"; }
            if (!missing.empty()) { MessageBoxW(hw, (L"These programs are missing from the tools folder:\n" + missing).c_str(), L"Missing program", MB_ICONERROR); return 0; }
            int reps = (int)SendMessageW(g_reps, CB_GETCURSEL, 0, 0) * 2 + 1; if (reps < 1) reps = 1;
            int ord = (int)SendMessageW(g_order, CB_GETCURSEL, 0, 0); g_mi = mi; { wchar_t ob[128]; GetWindowTextW(g_order, ob, 128); g_orderText = ob; } set_busy(true); ListView_DeleteAllItems(g_list);
            thread(worker, mi, oi, g_path, reps, ord).detach(); }
        else if (id == ID_COPY) copy_report();
        return 0; }
    case WM_CTLCOLORSTATIC: if ((HWND)lp == g_note) { SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_INFOBK); } break;
    case WM_CLOSE: if (g_busy && MessageBoxW(hw, L"A test is running. Close anyway?", L"Harrington Head-to-Head", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0; DestroyWindow(hw); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    wchar_t self[4096]; GetModuleFileNameW(NULL, self, 4096); g_dir = self; g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1);
    g_tools = g_dir + L"tools\\"; g_test = g_dir + L"testfiles\\"; CreateDirectoryW(g_test.c_str(), NULL);
    wchar_t tp[MAX_PATH]; GetTempPathW(MAX_PATH, tp); g_tmp = W(tp) + L"harrington-h2h\\"; CreateDirectoryW(g_tmp.c_str(), NULL);
    build_catalogue();
    {   // self-test: Harrington-Head-to-Head.exe /selftest FILE REPORT  - runs every fixed matchup on FILE, writes a text report
        int ac; wchar_t **av = CommandLineToArgvW(GetCommandLineW(), &ac);
        if (ac == 4 && !wcscmp(av[1], L"/selftest")) {
            W all; g_path = av[2]; g_sha = sha_file(g_path); g_orderText = L"Alternate every repeat (fairest)";
            for (int mi = 0; mi < (int)M.size(); mi++) for (int oi = 0; oi < (int)M[mi].opp.size(); oi++) { g_mi = mi; worker(mi, oi, g_path, 3, 0); show_results(); all += g_report + L"\r\n"; }
            FILE *f = _wfopen(av[3], L"wb"); if (f) { fputs("\xEF\xBB\xBF", f); int n = WideCharToMultiByte(CP_UTF8, 0, all.c_str(), -1, NULL, 0, NULL, NULL); string u(n, 0);
                WideCharToMultiByte(CP_UTF8, 0, all.c_str(), -1, &u[0], n, NULL, NULL); fwrite(u.data(), 1, n - 1, f); fclose(f); }
            return 0; }
        LocalFree(av);
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = wnd; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"HarringtonH2H"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(2)); RegisterClassW(&wc);
    int dpi = GetDpiForSystem();
    HWND w = CreateWindowExW(0, L"HarringtonH2H", L"Harrington Head-to-Head 1.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(980, dpi, 96), MulDiv(720, dpi, 96), NULL, NULL, hi, NULL);
    ShowWindow(w, show); MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) { if (!IsDialogMessageW(w, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } } return 0;
}
