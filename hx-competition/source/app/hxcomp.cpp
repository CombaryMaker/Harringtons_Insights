// hxcomp.cpp - HX Competition Test: HX Fast and HX Complete against published compressors, every result shown.
// For scientific and peer review purposes only. Patent Pending (U.S. Provisional App. 64/192,843).
// Copyright (c) 2026 Michael Hugh Harrington. This app's source: MIT License. HX itself: see HX/LICENSE.txt.
//
// Rules built into the program (see METHOD.txt):
//  - every program is run as a separate process with the exact command line shown in METHOD.txt and in the Advanced log;
//  - every compressed file is decoded with that program's own decoder and compared byte for byte (SHA-256);
//  - every number is measured on this computer; nothing is estimated; a failed or inexact result is shown, not hidden;
//  - every result is shown, the poor ones too; a tester can leave programs out, and the log lists what was left out;
//  - speed tiers are measured against LZ4 -1 decoding the same files on this computer (same rule as the 6 Nov 2026 chart).
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
#include <map>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
using namespace std;
typedef wstring W;
static const wchar_t *TITLE = L"For scientific and peer review purposes only, HX Competition Test, Patent Pending";
static const wchar_t *VERSION = L"HX Competition Test 1.0";
static RECT g_panels[3], g_titleRect; static HFONT g_font, g_bold, g_titleFont, g_mono; static int g_dpi = 96;
static int S(int v) { return MulDiv(v, g_dpi, 96); }
static HBRUSH g_panelBrush; static COLORREF PANEL = RGB(233, 233, 233);
#include "common.inc"

// ------------------------------------------------------------------ the programs in the race
// placeholders: {t} tools folder, {i} input, {o} output, {n} logical cores. cOut/dOut: the program writes to standard output.
struct Entry { const wchar_t *name; bool ours; const wchar_t *comp, *decomp; bool cOut, dOut; const wchar_t *ext; int kind; const wchar_t *source; };
enum { K_PLAIN = 0, K_HXFAST = 1, K_HXCOMPLETE = 2 };
static const Entry E[] = {
    // ---- Michael Harrington's software ("ours")
    {L"HX Fast", true, L"\"{t}hx.exe\" c \"{i}\" \"{o}\" {n}", L"\"{t}hx.exe\" d \"{i}\" \"{o}\" {n}", false, false, L".hx", K_HXFAST, L"HX (Michael Harrington, patent pending), source in source\\HX"},
    {L"HX Complete", true, L"\"{t}hx.exe\" c \"{i}\" \"{o}\" {n} --complete --no-learn", L"\"{t}hx.exe\" d \"{i}\" \"{o}\" {n}", false, false, L".hx", K_HXCOMPLETE, L"HX with HCM, H-Deflate and the format specialists (see the list in the log)"},
    {L"hs_zstd 1.3 level 3 (Harrington's Sets)", true, L"\"{t}hs_zstd.exe\" c \"{i}\" \"{o}\" --level 3 --jobs {n}", L"\"{t}hs_zstd.exe\" d \"{i}\" \"{o}\" --jobs {n}", false, false, L".hsz", K_PLAIN, L"Michael Harrington, MIT"},
    {L"hs_zstd 1.3 level 19 (Harrington's Sets)", true, L"\"{t}hs_zstd.exe\" c \"{i}\" \"{o}\" --level 19 --jobs {n}", L"\"{t}hs_zstd.exe\" d \"{i}\" \"{o}\" --jobs {n}", false, false, L".hsz", K_PLAIN, L"Michael Harrington, MIT"},
    {L"rd-zstd (record delta + zstd -19)", true, L"\"{t}rd-zstd.exe\" c \"{i}\" \"{o}\"", L"\"{t}rd-zstd.exe\" d \"{i}\" \"{o}\"", false, false, L".rdz", K_PLAIN, L"Michael Harrington, MIT"},
    {L"xz-hi (standard .xz, decoded by stock xz)", true, L"\"{t}xz-hi.exe\" \"{i}\" \"{o}\"", L"\"{t}xz.exe\" -d -c \"{i}\"", false, true, L".xz", K_PLAIN, L"Michael Harrington, MIT"},
    {L"bzip2-hi (standard .bz2, decoded by stock libbz2)", true, L"\"{t}bzip2-hi.exe\" c \"{i}\" \"{o}\"", L"\"{t}bzip2-stock.exe\" d \"{i}\" \"{o}\"", false, false, L".bz2", K_PLAIN, L"Michael Harrington, MIT"},
    {L"lzav-hi (LZAV format, decoded by stock LZAV)", true, L"\"{t}lzav-hi.exe\" c \"{i}\" \"{o}\"", L"\"{t}lzav-hi.exe\" d \"{i}\" \"{o}\"", false, false, L".lzav", K_PLAIN, L"Michael Harrington, MIT; LZAV by Aleksey Vaneev, MIT"},
    // ---- published compressors, stock (the fast group first)
    {L"LZ4 1.11.0 -1", false, L"\"{t}lz4.exe\" -1 -q -f \"{i}\" \"{o}\"", L"\"{t}lz4.exe\" -d -q -f \"{i}\" \"{o}\"", false, false, L".lz4", K_PLAIN, L"github.com/lz4/lz4 tag v1.11.0, BSD/GPL"},
    {L"LZ4 1.11.0 -9 (HC)", false, L"\"{t}lz4.exe\" -9 -q -f \"{i}\" \"{o}\"", L"\"{t}lz4.exe\" -d -q -f \"{i}\" \"{o}\"", false, false, L".lz4", K_PLAIN, L"github.com/lz4/lz4 tag v1.11.0"},
    {L"LZ4 1.11.0 -12 (HC max)", false, L"\"{t}lz4.exe\" -12 -q -f \"{i}\" \"{o}\"", L"\"{t}lz4.exe\" -d -q -f \"{i}\" \"{o}\"", false, false, L".lz4", K_PLAIN, L"github.com/lz4/lz4 tag v1.11.0"},
    {L"Snappy 1.3.1", false, L"\"{t}rivals.exe\" snappy c \"{i}\" \"{o}\"", L"\"{t}rivals.exe\" snappy d \"{i}\" \"{o}\"", false, false, L".snappy", K_PLAIN, L"github.com/google/snappy tag 1.3.1, BSD-3 (through rivals.exe)"},
    {L"LZAV 7.1 HI (stock encoder)", false, L"\"{t}lzav-hi.exe\" s \"{i}\" \"{o}\"", L"\"{t}lzav-hi.exe\" d \"{i}\" \"{o}\"", false, false, L".lzav", K_PLAIN, L"LZAV 7.1 by Aleksey Vaneev, MIT, unmodified inside lzav-hi.exe"},
    {L"gzip -6 (libdeflate 1.26)", false, L"\"{t}rivals.exe\" gzip 6 c \"{i}\" \"{o}\"", L"\"{t}rivals.exe\" gzip d \"{i}\" \"{o}\"", false, false, L".gz", K_PLAIN, L"github.com/ebiggers/libdeflate tag v1.26, MIT (through rivals.exe)"},
    {L"gzip -12 (libdeflate 1.26, max)", false, L"\"{t}rivals.exe\" gzip 12 c \"{i}\" \"{o}\"", L"\"{t}rivals.exe\" gzip d \"{i}\" \"{o}\"", false, false, L".gz", K_PLAIN, L"github.com/ebiggers/libdeflate tag v1.26"},
    {L"zstd 1.5.6 -1", false, L"\"{t}zstd.exe\" -q -f -1 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6, BSD"},
    {L"zstd 1.5.6 -3", false, L"\"{t}zstd.exe\" -q -f -3 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6"},
    {L"zstd 1.5.6 -9", false, L"\"{t}zstd.exe\" -q -f -9 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6"},
    {L"zstd 1.5.6 -19", false, L"\"{t}zstd.exe\" -q -f -19 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6"},
    {L"zstd 1.5.6 -19 --long=27", false, L"\"{t}zstd.exe\" -q -f -19 --long=27 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d --long=27 \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6"},
    {L"Brotli 1.2.0 -q 1", false, L"\"{t}brotli.exe\" -q 1 -w 24 -f -o \"{o}\" \"{i}\"", L"\"{t}brotli.exe\" -d -f -o \"{o}\" \"{i}\"", false, false, L".br", K_PLAIN, L"github.com/google/brotli v1.2.0, MIT"},
    {L"Brotli 1.2.0 -q 5", false, L"\"{t}brotli.exe\" -q 5 -w 24 -f -o \"{o}\" \"{i}\"", L"\"{t}brotli.exe\" -d -f -o \"{o}\" \"{i}\"", false, false, L".br", K_PLAIN, L"github.com/google/brotli v1.2.0"},
    {L"Brotli 1.2.0 -q 9", false, L"\"{t}brotli.exe\" -q 9 -w 24 -f -o \"{o}\" \"{i}\"", L"\"{t}brotli.exe\" -d -f -o \"{o}\" \"{i}\"", false, false, L".br", K_PLAIN, L"github.com/google/brotli v1.2.0"},
    // ---- published compressors, stock (the strong group)
    {L"Brotli 1.2.0 -q 11", false, L"\"{t}brotli.exe\" -q 11 -w 24 -f -o \"{o}\" \"{i}\"", L"\"{t}brotli.exe\" -d -f -o \"{o}\" \"{i}\"", false, false, L".br", K_PLAIN, L"github.com/google/brotli v1.2.0"},
    {L"zstd 1.5.6 --ultra -22 --long=31", false, L"\"{t}zstd.exe\" -q -f --ultra -22 --long=31 \"{i}\" -o \"{o}\"", L"\"{t}zstd.exe\" -q -f -d --long=31 \"{i}\" -o \"{o}\"", false, false, L".zst", K_PLAIN, L"github.com/facebook/zstd v1.5.6"},
    {L"bzip2 -9 (libbz2 1.1)", false, L"\"{t}bzip2-stock.exe\" c \"{i}\" \"{o}\"", L"\"{t}bzip2-stock.exe\" d \"{i}\" \"{o}\"", false, false, L".bz2", K_PLAIN, L"libbz2 (Julian Seward), BSD-style"},
    {L"xz 5.6.3 -9e", false, L"\"{t}xz.exe\" -9e -k -c \"{i}\"", L"\"{t}xz.exe\" -d -c \"{i}\"", true, true, L".xz", K_PLAIN, L"XZ Utils 5.6.3 (tukaani.org), 0BSD"},
    {L"7-Zip 26.06 -mx9 LZMA2", false, L"\"{t}7za.exe\" a -t7z -mx9 -m0=LZMA2 -bso0 -bsp0 -bse0 \"{o}\" \"{i}\"", L"\"{t}7za.exe\" e -so \"{i}\"", false, true, L".7z", K_PLAIN, L"7-Zip 26.06 7za.exe (Igor Pavlov), LGPL - official 7z2606-extra.7z"},
    {L"7-Zip 26.06 -mx9 PPMd", false, L"\"{t}7za.exe\" a -t7z -mx9 -m0=PPMd -bso0 -bsp0 -bse0 \"{o}\" \"{i}\"", L"\"{t}7za.exe\" e -so \"{i}\"", false, true, L".7z", K_PLAIN, L"7-Zip 26.06 7za.exe (Igor Pavlov), LGPL"},
    {L"BSC 3.4.0 (default)", false, L"\"{t}bsc.exe\" e \"{i}\" \"{o}\"", L"\"{t}bsc.exe\" d \"{i}\" \"{o}\"", false, false, L".bsc", K_PLAIN, L"libbsc 3.4.0 (Ilya Grebnov), Apache 2.0"},
    {L"BSC 3.4.0 -b64e2 (best)", false, L"\"{t}bsc.exe\" e \"{i}\" \"{o}\" -b64e2", L"\"{t}bsc.exe\" d \"{i}\" \"{o}\"", false, false, L".bsc", K_PLAIN, L"libbsc 3.4.0 (Ilya Grebnov), Apache 2.0"},
    {L"bzip3 1.5.1", false, L"\"{t}bzip3.exe\" -e -f -c \"{i}\"", L"\"{t}bzip3.exe\" -d -f -c \"{i}\"", true, true, L".bz3", K_PLAIN, L"bzip3 1.5.1 (Kamila Szewczyk), LGPL"},
    {L"kanzi 2.6.0 -l 1", false, L"\"{t}kanzi.exe\" -c -i \"{i}\" -o \"{o}\" -l 1 -f -v 0", L"\"{t}kanzi.exe\" -d -i \"{i}\" -o \"{o}\" -f -v 0", false, false, L".knz", K_PLAIN, L"github.com/flanglet/kanzi-cpp tag 2.6.0, Apache 2.0"},
    {L"kanzi 2.6.0 -l 3", false, L"\"{t}kanzi.exe\" -c -i \"{i}\" -o \"{o}\" -l 3 -f -v 0", L"\"{t}kanzi.exe\" -d -i \"{i}\" -o \"{o}\" -f -v 0", false, false, L".knz", K_PLAIN, L"kanzi-cpp 2.6.0"},
    {L"kanzi 2.6.0 -l 5", false, L"\"{t}kanzi.exe\" -c -i \"{i}\" -o \"{o}\" -l 5 -f -v 0", L"\"{t}kanzi.exe\" -d -i \"{i}\" -o \"{o}\" -f -v 0", false, false, L".knz", K_PLAIN, L"kanzi-cpp 2.6.0"},
    {L"kanzi 2.6.0 -l 7", false, L"\"{t}kanzi.exe\" -c -i \"{i}\" -o \"{o}\" -l 7 -f -v 0", L"\"{t}kanzi.exe\" -d -i \"{i}\" -o \"{o}\" -f -v 0", false, false, L".knz", K_PLAIN, L"kanzi-cpp 2.6.0"},
    {L"kanzi 2.6.0 -l 9", false, L"\"{t}kanzi.exe\" -c -i \"{i}\" -o \"{o}\" -l 9 -f -v 0", L"\"{t}kanzi.exe\" -d -i \"{i}\" -o \"{o}\" -f -v 0", false, false, L".knz", K_PLAIN, L"kanzi-cpp 2.6.0"},
};
static const int NE = (int)(sizeof E / sizeof E[0]);
static const int LZ4_REF = 8;   // index of "LZ4 1.11.0 -1", the speed reference
static const double TIER_UP[10] = {2, 5, 10, 20, 50, 100, 200, 500, 2000, 1e300};   // decode time up to x times LZ4's
static W exe_of(const Entry &e) { W c = e.comp; size_t a = c.find(L"{t}"), b = c.find(L".exe"); return a == W::npos || b == W::npos ? W() : c.substr(a + 3, b + 4 - a - 3); }
static W fill(const wchar_t *t, const W &i, const W &o) {
    W s = t; auto rep = [&](const W &k, const W &v) { size_t p; while ((p = s.find(k)) != W::npos) s.replace(p, k.size(), v); };
    rep(L"{t}", g_tools); rep(L"{i}", i); rep(L"{o}", o); rep(L"{n}", to_wstring(cores())); return s;
}

// ------------------------------------------------------------------ window state
enum { ID_LOAD = 101, ID_FOLDER, ID_FOLDERCHK, ID_RUNS, ID_BASIC, ID_ADV, ID_RUN, ID_CANCEL, ID_SAVE, ID_LOG, ID_STATUS, ID_FILE, ID_LIST, ID_ALL, ID_NONE, ID_HELP };
#define WM_STATUS (WM_APP + 1)
#define WM_DONE (WM_APP + 2)
static HWND g_wnd, g_load, g_folder, g_folderchk, g_file, g_runs, g_basic, g_adv, g_run, g_cancelb, g_save, g_log, g_status, g_list, g_all, g_none, g_help, g_lbl[4];
static W g_path, g_folderPath, g_logText, g_prevLog, g_csv; static bool g_busy = false, g_useFolder = false;
static void set_status(const W &s) { SetWindowTextW(g_status, s.c_str()); }
static void post_status(const W &s) { PostMessageW(g_wnd, WM_STATUS, 0, (LPARAM)new W(s)); }
static W log_box_text() { int n = GetWindowTextLengthW(g_log); W t(n, 0); if (n) GetWindowTextW(g_log, &t[0], n + 1); return t; }

// ------------------------------------------------------------------ one program on one file
struct Res { long long comp = -1; bool exact = false, failed = false; W err; vector<double> ct, dt, cc, dc; double cmem = 0, dmem = 0; W adv; };
static Res run_entry(const Entry &e, const W &in, long long orig, const W &shaIn, int runs, bool adv) {
    Res R; W tf = g_tmp + L"w"; W co = tf + e.ext, de = tf + L".dec", hdo = tf + L".hd", hdi = tf + L".hd.dec";
    bool hd = e.kind == K_HXCOMPLETE && fsize(g_tools + L"hdeflate.exe") > 0 && deflate_kind(in);
    for (int r = 0; r < runs && !g_cancel; r++) {
        double ct = 0, cc = 0, dt = 0, dc = 0; W cur = in; DeleteFileW(co.c_str()); DeleteFileW(de.c_str());
        if (hd) { Run h = run_cmd(q(g_tools + L"hdeflate.exe") + L" pack " + q(in) + L" " + q(hdo) + L" " + to_wstring(cores()), false);
            if (!h.ok) { R.failed = true; R.err = L"H-Deflate pack failed"; break; } ct += h.sec; cc += h.cpu; R.cmem = max(R.cmem, h.peakMB); cur = hdo; }
        W cl = fill(e.comp, cur, co); Run c = run_cmd(cl, false, e.cOut ? co : W());
        if (g_cancel) break; if (!c.ok || fsize(co) < 0) { R.failed = true; R.err = L"compress failed (exit " + to_wstring(c.code) + L")"; break; }
        ct += c.sec; cc += c.cpu; R.cmem = max(R.cmem, c.peakMB); R.comp = fsize(co);
        if (adv && r == 0) R.adv += L"    compress: " + cl + L"\r\n";
        W dl = fill(e.decomp, co, de); Run d = run_cmd(dl, false, e.dOut ? de : W());
        if (g_cancel) break; if (!d.ok) { R.failed = true; R.err = L"decompress failed (exit " + to_wstring(d.code) + L")"; break; }
        dt += d.sec; dc += d.cpu; R.dmem = max(R.dmem, d.peakMB); W back = de;
        if (adv && r == 0) R.adv += L"    decompress: " + dl + L"\r\n";
        if (hd) { Run h = run_cmd(q(g_tools + L"hdeflate.exe") + L" unpack " + q(de) + L" " + q(hdi) + L" " + to_wstring(cores()), false);
            if (!h.ok) { R.failed = true; R.err = L"H-Deflate unpack failed"; break; } dt += h.sec; dc += h.cpu; R.dmem = max(R.dmem, h.peakMB); back = hdi; }
        if (fsize(back) != orig || sha_file(back) != shaIn) { R.failed = true; R.err = L"decoded file does NOT match the original"; break; }
        R.ct.push_back(ct); R.cc.push_back(cc); R.dt.push_back(dt); R.dc.push_back(dc);
    }
    for (const W &f : {co, de, hdo, hdi}) DeleteFileW(f.c_str());
    R.exact = !R.failed && !R.ct.empty(); return R;
}

// ------------------------------------------------------------------ the test run (worker thread)
static W pad(W s, size_t n, bool right = false) { if (s.size() >= n) return s; return right ? W(n - s.size(), L' ') + s : s + W(n - s.size(), L' '); }
static int tier_of(double m) { for (int k = 0; k < 10; k++) if (m <= TIER_UP[k]) return k + 1; return 10; }
static W tier_rule(int k) { return k < 10 ? L"decode within " + fmtd(TIER_UP[k - 1], 0) + L"x LZ4's time" : L"any speed"; }
static void worker(vector<W> files, vector<int> sel, int runs, bool adv) {
    W log; SYSTEMTIME st; GetLocalTime(&st); wchar_t when[64]; swprintf(when, 64, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    log += W(TITLE) + L"\r\n" + VERSION + L"   " + when + L"\r\nComputer: " + machine_info() + L"\r\n";
    log += L"Runs per program per file: " + to_wstring(runs) + L" (times are the median; wall clock including program start-up). Threads: each program with its own default; HX uses all " + to_wstring(cores()) + L" cores.\r\n";
    log += L"Every result below is decoded by its own program and checked byte for byte (SHA-256). Every number is measured on this computer.\r\n";
    log += extras_text(true, adv);
    W left; for (int i = 0; i < NE; i++) if (find(sel.begin(), sel.end(), i) == sel.end()) left += L"  " + W(E[i].name) + L"\r\n";
    log += L"Programs in this test: " + to_wstring(sel.size()) + L" of " + to_wstring(NE) + L"\r\n";
    log += L"Left out by the tester: " + (left.empty() ? W(L"none\r\n") : L"\r\n" + left);
    if (adv) { log += L"Programs used (SHA-256):\r\n"; vector<W> seen; for (int i : sel) { W x = exe_of(E[i]); if (x.empty() || find(seen.begin(), seen.end(), x) != seen.end()) continue; seen.push_back(x);
        log += L"  " + x + L"  " + fmtn(fsize(g_tools + x)) + L" bytes  " + sha_file(g_tools + x) + L"\r\n"; } }
    log += L"\r\n"; PostMessageW(g_wnd, WM_STATUS, 1, (LPARAM)new W(log));
    W csv = L"file,program,group,orig_bytes,comp_bytes,ratio,comp_s,decomp_s,comp_cpu_s,decomp_cpu_s,comp_peak_MB,decomp_peak_MB,exact\r\n";
    vector<long long> tot_c(NE, 0); vector<double> tot_ct(NE, 0), tot_dt(NE, 0); vector<int> ok_files(NE, 0); long long tot_o = 0; int nfiles = 0;
    vector<vector<long long>> fc(files.size(), vector<long long>(NE, -1));
    for (size_t fi = 0; fi < files.size() && !g_cancel; fi++) {
        const W &f = files[fi]; long long orig = fsize(f); W shaIn = sha_file(f); vector<Res> R(NE);
        for (size_t k = 0; k < sel.size() && !g_cancel; k++) { int i = sel[k];
            post_status(L"File " + to_wstring(fi + 1) + L" of " + to_wstring(files.size()) + L" (" + basename(f) + L"), program " + to_wstring(k + 1) + L" of " + to_wstring(sel.size()) + L": " + E[i].name);
            R[i] = run_entry(E[i], f, orig, shaIn, runs, adv); }
        if (g_cancel) break;
        nfiles++; tot_o += orig;
        double lz4d = R[LZ4_REF].exact ? median(R[LZ4_REF].dt) : 0;
        vector<int> ord = sel; sort(ord.begin(), ord.end(), [&](int a, int b) { double x = R[a].exact ? median(R[a].dt) : 1e300, y = R[b].exact ? median(R[b].dt) : 1e300; return x < y; });
        log += L"File: " + basename(f) + L"   " + fmtn(orig) + L" bytes   SHA-256 " + shaIn + L"\r\n";
        log += L"  " + pad(L"Program", 52) + pad(L"Group", 7) + pad(L"Bytes", 14, true) + pad(L"Ratio", 9, true) + pad(L"Comp s", 10, true) + pad(L"Decomp s", 10, true) + pad(L"x LZ4", 9, true) + L"  Round trip\r\n";
        long long bestAll = -1, bestOurs = -1, bestRiv = -1; int iAll = -1, iOurs = -1, iRiv = -1;
        for (int i : ord) { const Res &x = R[i];
            if (!x.exact) { log += L"  " + pad(E[i].name, 52) + pad(E[i].ours ? L"ours" : L"rival", 7) + L"  " + x.err + L"\r\n";
                csv += L"\"" + basename(f) + L"\",\"" + E[i].name + L"\"," + (E[i].ours ? L"ours" : L"rival") + L"," + to_wstring(orig) + L",,,,,,,,,FAILED: " + x.err + L"\r\n"; continue; }
            double ct = median(x.ct), dt = median(x.dt); double m = lz4d > 0 ? dt / lz4d : 0;
            log += L"  " + pad(E[i].name, 52) + pad(E[i].ours ? L"ours" : L"rival", 7) + pad(fmtn(x.comp), 14, true) + pad(fmtd((double)orig / max<long long>(1, x.comp), 3) + L"x", 9, true)
                 + pad(fmtd(ct, 3), 10, true) + pad(fmtd(dt, 3), 10, true) + pad(lz4d > 0 ? fmtd(m, 2) : W(L"-"), 9, true) + L"  exact\r\n";
            if (adv) log += x.adv;
            csv += L"\"" + basename(f) + L"\",\"" + E[i].name + L"\"," + (E[i].ours ? L"ours" : L"rival") + L"," + to_wstring(orig) + L"," + to_wstring(x.comp) + L"," + fmtd((double)orig / max<long long>(1, x.comp), 4) + L","
                 + fmtd(ct, 4) + L"," + fmtd(dt, 4) + L"," + (median(x.cc) > 0 ? fmtd(median(x.cc), 3) : W(L"not measured")) + L"," + (median(x.dc) > 0 ? fmtd(median(x.dc), 3) : W(L"not measured")) + L","
                 + fmtd(x.cmem, 1) + L"," + fmtd(x.dmem, 1) + L",exact\r\n";
            fc[fi][i] = x.comp; tot_c[i] += x.comp; tot_ct[i] += ct; tot_dt[i] += dt; ok_files[i]++;
            if (bestAll < 0 || x.comp < bestAll) { bestAll = x.comp; iAll = i; }
            if (E[i].ours && (bestOurs < 0 || x.comp < bestOurs)) { bestOurs = x.comp; iOurs = i; }
            if (!E[i].ours && (bestRiv < 0 || x.comp < bestRiv)) { bestRiv = x.comp; iRiv = i; } }
        if (iAll >= 0) log += L"  Smallest overall: " + W(E[iAll].name) + L" (" + fmtn(bestAll) + L" bytes)\r\n";
        if (iOurs >= 0 && iRiv >= 0) log += L"  Smallest of ours: " + W(E[iOurs].name) + L" " + fmtn(bestOurs) + L"   smallest rival: " + E[iRiv].name + L" " + fmtn(bestRiv) + L"   ("
            + (bestOurs <= bestRiv ? L"ours " + fmtd(100.0 * (bestRiv - bestOurs) / bestRiv, 2) + L"% smaller" : L"rival " + fmtd(100.0 * (bestOurs - bestRiv) / bestOurs, 2) + L"% smaller") + L")\r\n";
        if (lz4d <= 0) log += L"  (LZ4 -1 was left out or failed, so no speed tiers for this file)\r\n";
        log += L"\r\n"; PostMessageW(g_wnd, WM_STATUS, 1, (LPARAM)new W(log));
    }
    // ---- totals and races (programs that finished every file exactly)
    if (nfiles > 0) {
        vector<int> full; for (int i : sel) if (ok_files[i] == nfiles) full.push_back(i);
        bool haveRef = find(full.begin(), full.end(), LZ4_REF) != full.end(); double lz4T = haveRef ? tot_dt[LZ4_REF] : 0;
        auto ratio = [&](int i) { return (double)tot_o / max<long long>(1, tot_c[i]); };
        sort(full.begin(), full.end(), [&](int a, int b) { return tot_c[a] < tot_c[b]; });
        log += L"TOTAL for " + to_wstring(nfiles) + L" file" + (nfiles > 1 ? L"s" : L"") + L", " + fmtn(tot_o) + L" bytes (programs that finished every file exactly; sorted smallest first)\r\n";
        log += L"  " + pad(L"Program", 52) + pad(L"Group", 7) + pad(L"Bytes", 14, true) + pad(L"Ratio", 9, true) + pad(L"Comp s", 10, true) + pad(L"Decomp s", 10, true) + pad(L"x LZ4", 9, true) + L"  Tier\r\n";
        for (int i : full) { double m = lz4T > 0 ? tot_dt[i] / lz4T : 0;
            log += L"  " + pad(E[i].name, 52) + pad(E[i].ours ? L"ours" : L"rival", 7) + pad(fmtn(tot_c[i]), 14, true) + pad(fmtd(ratio(i), 3) + L"x", 9, true) + pad(fmtd(tot_ct[i], 3), 10, true)
                 + pad(fmtd(tot_dt[i], 3), 10, true) + pad(lz4T > 0 ? fmtd(m, 2) : W(L"-"), 9, true) + L"  " + (lz4T > 0 ? to_wstring(tier_of(m)) : W(L"-")) + L"\r\n"; }
        for (int i : sel) if (ok_files[i] != nfiles) log += L"  " + pad(E[i].name, 52) + L"not in the totals (failed or was cancelled on " + to_wstring(nfiles - ok_files[i]) + L" file(s))\r\n";
        auto race = [&](const wchar_t *title, int hxi, bool fast) {
            if (find(full.begin(), full.end(), hxi) == full.end()) return;
            log += L"\r\n" + W(title) + L"\r\n";
            vector<int> v; for (int i : full) { if (E[i].ours && i != hxi) continue;
                if (fast ? tot_dt[i] <= 2.0 * tot_dt[hxi] : ratio(i) >= 0.9 * ratio(hxi)) v.push_back(i); }
            sort(v.begin(), v.end(), [&](int a, int b) { return tot_c[a] < tot_c[b]; }); int place = 0;
            for (int i : v) { place++; log += L"  " + pad(to_wstring(place) + L".", 5) + pad(E[i].name, 52) + pad(fmtn(tot_c[i]), 14, true) + pad(fmtd(ratio(i), 3) + L"x", 9, true)
                 + L"   decode " + fmtd(tot_dt[i], 3) + L" s   compress " + fmtd(tot_ct[i], 3) + L" s" + (i == hxi ? L"   <- " + W(E[i].name) : W()) + L"\r\n"; }
            int better = 0; for (int i : v) if (i != hxi && tot_c[i] < tot_c[hxi]) better++;
            log += L"  " + W(E[hxi].name) + L" places " + to_wstring(better + 1) + L" of " + to_wstring(v.size()) + L" in this race.\r\n"; };
        race(L"HX FAST RACE - every program decoding the files in no more than 2x HX Fast's decode time, smallest first", 0, true);
        race(L"HX COMPLETE RACE - every program within 10% of HX Complete's ratio or better, smallest first", 1, false);
        if (lz4T > 0) {
            log += L"\r\nSPEED TIERS (same rule as the 6 Nov 2026 chart; a program counts in a tier if it decodes within that tier's limit)\r\n";
            log += L"  LZ4 -1 total decode time on this computer: " + fmtd(lz4T, 3) + L" s\r\n";
            if (lz4T < 0.5) log += L"  NOTE: LZ4 decoded these files in under 0.5 s, so the times are mostly program start-up and the tiers below say little about speed. Use larger files for speed tiers.\r\n";
            int wins = 0, cells = 0;
            for (int k = 1; k <= 10; k++) { int bo = -1, br = -1;
                for (int i : full) { double m = tot_dt[i] / lz4T; if (m > TIER_UP[k - 1]) continue;
                    if (E[i].ours) { if (bo < 0 || tot_c[i] < tot_c[bo]) bo = i; } else { if (br < 0 || tot_c[i] < tot_c[br]) br = i; } }
                W res; if (bo >= 0 && br >= 0) { cells++; bool w = tot_c[bo] <= tot_c[br]; if (w) wins++;
                    res = (w ? L"WIN   " : L"LOSS  ") + W(E[bo].name) + L" " + fmtd(ratio(bo), 3) + L"x vs " + E[br].name + L" " + fmtd(ratio(br), 3) + L"x"; }
                else if (bo >= 0) { cells++; wins++; res = L"WIN   " + W(E[bo].name) + L" " + fmtd(ratio(bo), 3) + L"x (no rival this fast)"; }
                else if (br >= 0) { cells++; res = L"LOSS  none of ours this fast; best rival " + W(E[br].name) + L" " + fmtd(ratio(br), 3) + L"x"; }
                else res = L"-     nothing this fast";
                log += L"  Tier " + pad(to_wstring(k), 3) + pad(L"(" + tier_rule(k) + L")", 34) + res + L"\r\n"; }
            log += L"  Harrington's compression concepts won " + to_wstring(wins) + L" of " + to_wstring(cells) + L" tiers for " + (g_useFolder ? L"the folder \"" + basename(g_folderPath) + L"\"" : L"the file \"" + basename(files[0]) + L"\"") + L".\r\n";
        } else log += L"\r\n(LZ4 -1 was left out or failed, so speed tiers cannot be measured.)\r\n";
    }
    if (g_cancel) log += L"\r\nCANCELLED by the user - results above are only for what finished.\r\n";
    PostMessageW(g_wnd, WM_STATUS, 2, (LPARAM)new W(csv));
    PostMessageW(g_wnd, WM_DONE, 0, (LPARAM)new W(log));
}

// ------------------------------------------------------------------ layout and window
static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD st, int id, HFONT f = NULL) {
    HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, 0, 0, 10, 10, g_wnd, (HMENU)(intptr_t)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : g_font), TRUE); return h;
}
static void layout() {
    RECT rc; GetClientRect(g_wnd, &rc); int w = rc.right, h = rc.bottom, m = S(14), y = S(10), x = m + S(10), iw = w - 2 * m - S(20);
    g_titleRect = {m, y, w - m, y + S(26)}; y += S(34);
    int p1 = y; y += S(10);
    MoveWindow(g_load, x, y, S(130), S(28), TRUE); MoveWindow(g_folderchk, x + S(142), y + S(4), S(290), S(22), TRUE); MoveWindow(g_folder, x + S(436), y, S(130), S(28), TRUE);
    MoveWindow(g_file, x + S(576), y + S(6), iw - S(576), S(20), TRUE); y += S(38);
    MoveWindow(g_lbl[0], x, y + S(5), S(44), S(20), TRUE); MoveWindow(g_runs, x + S(46), y, S(60), S(300), TRUE);
    MoveWindow(g_lbl[1], x + S(126), y + S(5), S(40), S(20), TRUE); MoveWindow(g_basic, x + S(168), y + S(3), S(100), S(22), TRUE); MoveWindow(g_adv, x + S(272), y + S(3), S(120), S(22), TRUE);
    MoveWindow(g_help, x + S(400), y + S(1), S(24), S(26), TRUE);
    MoveWindow(g_run, x + iw - S(380), y, S(120), S(30), TRUE); MoveWindow(g_cancelb, x + iw - S(254), y, S(120), S(30), TRUE); MoveWindow(g_save, x + iw - S(128), y, S(128), S(30), TRUE); y += S(40);
    g_panels[0] = {m, p1, w - m, y}; y += S(14);
    int listH = max(S(120), (h - y) * 2 / 5); int p2 = y; y += S(8);
    MoveWindow(g_lbl[2], x, y, S(400), S(20), TRUE); MoveWindow(g_all, x + iw - S(250), y - S(3), S(120), S(26), TRUE); MoveWindow(g_none, x + iw - S(124), y - S(3), S(124), S(26), TRUE); y += S(26);
    MoveWindow(g_list, x, y, iw, listH - S(40), TRUE); y += listH - S(40) + S(8); g_panels[1] = {m, p2, w - m, y}; y += S(14);
    int p3 = y; y += S(8); int logh = h - y - S(44); MoveWindow(g_log, x, y, iw, max(S(80), logh), TRUE); y += max(S(80), logh) + S(8); g_panels[2] = {m, p3, w - m, y};
    MoveWindow(g_status, m, h - S(28), w - 2 * m, S(22), TRUE);
    InvalidateRect(g_wnd, NULL, TRUE);
}
static void set_busy(bool b) { g_busy = b; SendMessageW(g_log, EM_SETREADONLY, b ? TRUE : FALSE, 0);
    for (HWND hh : {g_load, g_folder, g_folderchk, g_runs, g_basic, g_adv, g_run, g_save, g_list, g_all, g_none}) EnableWindow(hh, !b); EnableWindow(g_cancelb, b); }
static void update_file_label() {
    W t; if (g_useFolder) t = g_folderPath.empty() ? W(L"No folder chosen.") : L"Folder: " + g_folderPath; else t = g_path.empty() ? W(L"No file loaded.") : L"File: " + basename(g_path) + L" (" + human(fsize(g_path)) + L")";
    SetWindowTextW(g_file, t.c_str()); }
static void save_log() {
    g_logText = log_box_text(); if (g_logText.empty()) { MessageBoxW(g_wnd, L"The log is empty.", L"Save log", MB_ICONINFORMATION); return; }
    SYSTEMTIME st; GetLocalTime(&st); wchar_t fn[MAX_PATH]; swprintf(fn, MAX_PATH, L"HX-Competition-Log-%04d-%02d-%02d-%02d%02d%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    OPENFILENAMEW of = {sizeof of}; of.hwndOwner = g_wnd; of.lpstrFile = fn; of.nMaxFile = MAX_PATH; of.lpstrFilter = L"Text file (*.txt)\0*.txt\0All files\0*.*\0";
    of.lpstrInitialDir = g_dir.c_str(); of.lpstrDefExt = L"txt"; of.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER; if (!GetSaveFileNameW(&of)) return;
    auto wr = [](const W &path, const W &text) { FILE *f = _wfopen(path.c_str(), L"wb"); if (!f) return false; fputs("\xEF\xBB\xBF", f); int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
        string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &u[0], n, NULL, NULL); fwrite(u.data(), 1, n - 1, f); fclose(f); return true; };
    W p = fn; if (!wr(p, g_logText)) { MessageBoxW(g_wnd, L"Could not write the file.", L"Save log", MB_ICONERROR); return; }
    W cp = p; size_t dot = cp.find_last_of(L'.'); cp = (dot == W::npos ? cp : cp.substr(0, dot)) + L".csv"; if (!g_csv.empty()) wr(cp, g_csv);
    set_status(L"Log saved: " + p + (g_csv.empty() ? W() : L"  (results table: " + basename(cp) + L")")); }
static void start_run() {
    vector<W> files;
    if (g_useFolder) { if (g_folderPath.empty()) { MessageBoxW(g_wnd, L"Choose a folder first.", L"HX Competition Test", MB_ICONINFORMATION); return; }
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((g_folderPath + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(g_folderPath + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd)); FindClose(h); }
        sort(files.begin(), files.end()); }
    else { if (g_path.empty()) { MessageBoxW(g_wnd, L"Load a file first.", L"HX Competition Test", MB_ICONINFORMATION); return; } files.push_back(g_path); }
    if (files.empty()) { MessageBoxW(g_wnd, L"The folder has no files.", L"HX Competition Test", MB_ICONINFORMATION); return; }
    vector<int> sel; W missing;
    for (int i = 0; i < NE; i++) if (ListView_GetCheckState(g_list, i)) { W x = exe_of(E[i]); if (!x.empty() && fsize(g_tools + x) < 0) missing += L"  " + W(E[i].name) + L" (" + x + L")\r\n"; else sel.push_back(i); }
    if (!missing.empty()) { MessageBoxW(g_wnd, (L"These programs are not in the tools folder and will be left out:\r\n" + missing).c_str(), L"HX Competition Test", MB_ICONINFORMATION); }
    if (sel.empty()) { MessageBoxW(g_wnd, L"Tick at least one program.", L"HX Competition Test", MB_ICONINFORMATION); return; }
    int runs = (int)SendMessageW(g_runs, CB_GETCURSEL, 0, 0) + 1; bool adv = SendMessageW(g_adv, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_logText = log_box_text(); g_prevLog = g_logText.empty() ? W() : g_logText + L"\r\n==================================================================== next test\r\n\r\n";
    g_cancel = false; g_csv.clear(); set_busy(true); CreateDirectoryW(g_tmp.c_str(), NULL);
    thread(worker, files, sel, runs, adv).detach();
}
static const wchar_t *HELP =
    L"HX COMPETITION TEST\r\n\r\n"
    L"Every ticked program compresses and decompresses each file; every result is checked byte for byte and shown, the poor ones too.\r\n\r\n"
    L"OURS: HX Fast, HX Complete (with HCM, H-Deflate and the format specialists) and Michael Harrington's other tools (hs_zstd, rd-zstd, xz-hi, "
    L"bzip2-hi, lzav-hi). They are never scored against each other.\r\n\r\n"
    L"RIVALS: stock builds of LZ4, Snappy, LZAV, gzip (libdeflate), zstd, Brotli, bzip2, xz, 7-Zip (LZMA2 and PPMd), BSC, bzip3 and kanzi, at the "
    L"levels listed. paq8px, cmix and ZPAQ are not included: they are far slower than everything else here.\r\n\r\n"
    L"HX FAST RACE: every program that decodes the files in no more than twice HX Fast's decode time, ranked by size.\r\n"
    L"HX COMPLETE RACE: every program within 10% of HX Complete's ratio or better, ranked by size.\r\n"
    L"SPEED TIERS: a program's decode time divided by LZ4 -1's decode time on the same files, on this computer. Tier 1 = within 2x, then 5x, 10x, "
    L"20x, 50x, 100x, 200x, 500x, 2000x, and tier 10 = any speed. In each tier the smallest of ours is compared with the smallest rival.\r\n\r\n"
    L"Times are wall clock including program start-up, so very small files mostly measure start-up. Untick any program to leave it out; the log lists "
    L"what was left out. Save log... also writes every number to a .csv file next to the log.";
static LRESULT CALLBACK wnd(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hw; g_dpi = GetDpiForWindow(hw); g_panelBrush = CreateSolidBrush(PANEL);
        NONCLIENTMETRICSW ncm = {sizeof ncm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont; lf.lfHeight = -S(13); g_font = CreateFontIndirectW(&lf); lf.lfWeight = FW_SEMIBOLD; g_bold = CreateFontIndirectW(&lf);
        lf.lfHeight = -S(17); lf.lfWeight = FW_BOLD; g_titleFont = CreateFontIndirectW(&lf);
        LOGFONTW mf = {}; mf.lfHeight = -S(12); wcscpy(mf.lfFaceName, L"Consolas"); g_mono = CreateFontIndirectW(&mf);
        g_load = mk(L"BUTTON", L"Load file...", BS_PUSHBUTTON | WS_TABSTOP, ID_LOAD);
        g_folderchk = mk(L"BUTTON", L"Run all files in a folder (totals at the end)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_FOLDERCHK);
        g_folder = mk(L"BUTTON", L"Choose folder...", BS_PUSHBUTTON | WS_TABSTOP, ID_FOLDER); g_file = mk(L"STATIC", L"No file loaded.", SS_LEFT | SS_PATHELLIPSIS, ID_FILE);
        g_lbl[0] = mk(L"STATIC", L"Runs:", 0, 0, g_bold); g_runs = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_RUNS);
        for (int i = 1; i <= 10; i++) SendMessageW(g_runs, CB_ADDSTRING, 0, (LPARAM)to_wstring(i).c_str()); SendMessageW(g_runs, CB_SETCURSEL, 0, 0);
        g_lbl[1] = mk(L"STATIC", L"Log:", 0, 0, g_bold);
        g_basic = mk(L"BUTTON", L"Basic log", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, ID_BASIC); g_adv = mk(L"BUTTON", L"Advanced log", BS_AUTORADIOBUTTON, ID_ADV); SendMessageW(g_basic, BM_SETCHECK, BST_CHECKED, 0);
        g_help = mk(L"BUTTON", L"?", BS_PUSHBUTTON | WS_TABSTOP, ID_HELP);
        g_run = mk(L"BUTTON", L"Run test", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_RUN); g_cancelb = mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, ID_CANCEL);
        g_save = mk(L"BUTTON", L"Save log...", BS_PUSHBUTTON | WS_TABSTOP, ID_SAVE); EnableWindow(g_cancelb, FALSE);
        g_lbl[2] = mk(L"STATIC", L"Programs in the test (untick any to leave it out):", 0, 0, g_bold);
        g_all = mk(L"BUTTON", L"Tick all", BS_PUSHBUTTON | WS_TABSTOP, ID_ALL); g_none = mk(L"BUTTON", L"Untick all", BS_PUSHBUTTON | WS_TABSTOP, ID_NONE);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOSORTHEADER | LVS_SHOWSELALWAYS, 0, 0, 10, 10, hw, (HMENU)ID_LIST, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE); ListView_SetExtendedListViewStyle(g_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.cx = S(380); c.pszText = (LPWSTR)L"Program"; ListView_InsertColumn(g_list, 0, &c);
          c.cx = S(70); c.pszText = (LPWSTR)L"Group"; ListView_InsertColumn(g_list, 1, &c); c.cx = S(560); c.pszText = (LPWSTR)L"Source / version"; ListView_InsertColumn(g_list, 2, &c); }
        for (int i = 0; i < NE; i++) { LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)E[i].name; ListView_InsertItem(g_list, &it);
            ListView_SetItemText(g_list, i, 1, (LPWSTR)(E[i].ours ? L"ours" : L"rival")); ListView_SetItemText(g_list, i, 2, (LPWSTR)E[i].source); ListView_SetCheckState(g_list, i, TRUE); }
        g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL, 0, 0, 10, 10, hw, (HMENU)ID_LOG, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_log, WM_SETFONT, (WPARAM)g_mono, TRUE); SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
        g_status = mk(L"STATIC", L"Load a file (or choose a folder), untick any programs you want left out, then press Run test.", SS_LEFT, ID_STATUS);
        layout(); return 0; }
    case WM_ERASEBKGND: { RECT rc; GetClientRect(hw, &rc); HDC dc = (HDC)wp; HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bm);
        paint_bg(mem, rc); BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem); return 1; }
    case WM_CTLCOLOREDIT: if ((HWND)lp == g_log) { HDC dc = (HDC)wp; SetBkColor(dc, RGB(248, 248, 246)); SetTextColor(dc, RGB(0, 0, 0)); static HBRUSH eb = CreateSolidBrush(RGB(248, 248, 246)); return (LRESULT)eb; } break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: { HDC dc = (HDC)wp; HWND c = (HWND)lp;
        if (c == g_log) { SetBkColor(dc, RGB(248, 248, 246)); static HBRUSH lb = CreateSolidBrush(RGB(248, 248, 246)); return (LRESULT)lb; }
        if (c == g_status) { static HBRUSH sbr = CreateSolidBrush(RGB(202, 202, 203)); SetBkColor(dc, RGB(202, 202, 203)); SetTextColor(dc, RGB(30, 30, 30)); return (LRESULT)sbr; }
        SetBkColor(dc, PANEL); SetTextColor(dc, RGB(25, 25, 25)); return (LRESULT)g_panelBrush; }
    case WM_SIZE: layout(); return 0;
    case WM_GETMINMAXINFO: { auto *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = S(760); mm->ptMinTrackSize.y = S(560); return 0; }
    case WM_DPICHANGED: { g_dpi = HIWORD(wp); RECT *r = (RECT *)lp; SetWindowPos(hw, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); return 0; }
    case WM_STATUS: { W *s = (W *)lp; if (wp == 1) { W all = g_prevLog + *s; SetWindowTextW(g_log, all.c_str()); SendMessageW(g_log, EM_SETSEL, all.size(), all.size()); SendMessageW(g_log, EM_SCROLLCARET, 0, 0); }
        else if (wp == 2) g_csv = *s; else set_status(*s); delete s; return 0; }
    case WM_DONE: { W *s = (W *)lp; g_logText = g_prevLog + *s; delete s; SetWindowTextW(g_log, g_logText.c_str()); SendMessageW(g_log, EM_SETSEL, g_logText.size(), g_logText.size()); SendMessageW(g_log, EM_SCROLLCARET, 0, 0); set_busy(false);
        set_status(g_cancel ? L"Cancelled. Temporary files were removed." : L"Finished. Press Save log... to keep the log (and the .csv results table)."); return 0; }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case ID_HELP: MessageBoxW(hw, HELP, L"HX Competition Test - how it works", MB_OK | MB_ICONINFORMATION); break;
        case ID_ALL: case ID_NONE: for (int i = 0; i < NE; i++) ListView_SetCheckState(g_list, i, LOWORD(wp) == ID_ALL); break;
        case ID_LOAD: { wchar_t fn[4096] = L""; OPENFILENAMEW of = {sizeof of}; of.hwndOwner = hw; of.lpstrFile = fn; of.nMaxFile = 4096; of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameW(&of)) { g_path = fn; g_useFolder = false; SendMessageW(g_folderchk, BM_SETCHECK, BST_UNCHECKED, 0); update_file_label(); } break; }
        case ID_FOLDERCHK: g_useFolder = SendMessageW(g_folderchk, BM_GETCHECK, 0, 0) == BST_CHECKED; update_file_label(); break;
        case ID_FOLDER: { BROWSEINFOW bi = {}; bi.hwndOwner = hw; bi.lpszTitle = L"Choose the folder of test files"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi); if (pid) { wchar_t p[MAX_PATH]; if (SHGetPathFromIDListW(pid, p)) { g_folderPath = p; g_useFolder = true; SendMessageW(g_folderchk, BM_SETCHECK, BST_CHECKED, 0); update_file_label(); } CoTaskMemFree(pid); } break; }
        case ID_RUN: if (!g_busy) start_run(); break;
        case ID_CANCEL: if (g_busy) { g_cancel = true; EnterCriticalSection(&g_jobLock); if (g_curJob) TerminateJobObject(g_curJob, 1); LeaveCriticalSection(&g_jobLock); set_status(L"Cancelling..."); } break;
        case ID_SAVE: save_log(); break;
        } return 0; }
    case WM_CLOSE: if (g_busy) { if (MessageBoxW(hw, L"A test is running. Stop it and close?", L"HX Competition Test", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g_cancel = true; EnterCriticalSection(&g_jobLock); if (g_curJob) TerminateJobObject(g_curJob, 1); LeaveCriticalSection(&g_jobLock); } DestroyWindow(hw); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}
void selftest_run(const W &f, const vector<int> &sel, const W &report);
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&ic); InitializeCriticalSection(&g_jobLock);
    wchar_t self[4096]; GetModuleFileNameW(NULL, self, 4096); g_dir = self; g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/") + 1);
    g_tools = g_dir + L"tools\\"; wchar_t tp[MAX_PATH]; GetTempPathW(MAX_PATH, tp); g_tmp = W(tp) + L"hx-competition\\"; CreateDirectoryW(g_tmp.c_str(), NULL);
    set_extra_env();
    {   // self-test: HX-Competition-Test.exe /selftest FILE REPORT.txt - every program, 1 run
        int ac; wchar_t **av = CommandLineToArgvW(GetCommandLineW(), &ac);
        if (ac >= 4 && !wcscmp(av[1], L"/selftest")) { wchar_t full[4096]; W f = GetFullPathNameW(av[2], 4096, full, NULL) ? W(full) : W(av[2]);
            vector<int> sel; for (int i = 0; i < NE; i++) { W x = exe_of(E[i]); if (x.empty() || fsize(g_tools + x) >= 0) sel.push_back(i); }
            selftest_run(f, sel, av[3]); return 0; }
        LocalFree(av);
    }
    WNDCLASSW wc = {}; wc.lpfnWndProc = wnd; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = NULL;
    wc.lpszClassName = L"HXCompetition"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(2)); RegisterClassW(&wc);
    // fit the window to the screen's work area (high-DPI screens included)
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); int dpi = GetDpiForSystem();
    int ww = min((int)(wa.right - wa.left) * 9 / 10, MulDiv(1200, dpi, 96)), wh = min((int)(wa.bottom - wa.top) * 9 / 10, MulDiv(940, dpi, 96));
    HWND w = CreateWindowExW(0, L"HXCompetition", L"HX Competition Test - Patent Pending", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, NULL, NULL, hi, NULL);
    ShowWindow(w, show); MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.hwnd == g_log && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageW(g_log, EM_SETSEL, 0, -1); continue; }
        if (!IsDialogMessageW(w, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } } return 0;
}
// self-test without the window: runs the same worker and writes its log to REPORT (and REPORT.csv)
static W g_stLog, g_stCsv;
void selftest_run(const W &f, const vector<int> &sel, const W &report) {
    // the worker posts to g_wnd; with no window we run it in a hidden message-only window and collect the text
    WNDCLASSW wc = {}; wc.lpszClassName = L"HXCompST"; wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_STATUS) { W *s = (W *)l; if (w == 2) g_stCsv = *s; delete s; return 0; }
        if (m == WM_DONE) { W *s = (W *)l; g_stLog = *s; delete s; PostQuitMessage(0); return 0; } return DefWindowProcW(h, m, w, l); };
    RegisterClassW(&wc); g_wnd = CreateWindowExW(0, L"HXCompST", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    thread(worker, vector<W>{f}, sel, 1, true).detach(); MSG msg; while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    auto wr = [](const W &path, const W &text) { FILE *fp = _wfopen(path.c_str(), L"wb"); if (!fp) return; fputs("\xEF\xBB\xBF", fp); int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
        string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &u[0], n, NULL, NULL); fwrite(u.data(), 1, n - 1, fp); fclose(fp); };
    wr(report, g_stLog); wr(report + L".csv", g_stCsv);
}
