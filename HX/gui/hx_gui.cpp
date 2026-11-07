// hx_gui.cpp - HX Compressor, the Windows desktop app for HX and the common formats.
// Michael Hugh Harrington, November 2026. HX is Patent Pending (U.S. Provisional App. 64/192,843).
// Same license as HX (see LICENSE.txt): free for personal use and for research / peer review; commercial use needs a license.
//
// The window lets anyone pick files or a folder, choose a format and press Compress, or open an archive and Extract it.
// HX (.hx, Fast or Complete) is our own format; the others are made by the standard programs, which the app looks for
// in the "tools" folder beside it (see TOOLS.txt). A format whose program is missing is shown as unavailable.
// Every archive the app makes is opened again and compared with the input before it reports success.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <cstdio>
using namespace std;
typedef wstring W;

enum { ID_ADDF = 101, ID_ADDD, ID_CLEAR, ID_FMT, ID_FAST, ID_COMPLETE, ID_OUTDIR, ID_COMPRESS, ID_OPEN, ID_EXTRACT, ID_LIST, ID_LOG, ID_PROG, ID_VERIFY };
#define WM_JOBDONE (WM_APP + 1)
#define WM_LOGLINE (WM_APP + 2)

// ---------- formats ----------
enum Kind { ANY, AUDIO, IMAGE };
struct Fmt { const wchar_t *name, *ext, *tool, *note; Kind kind; bool multi; };
static const Fmt FMTS[] = {
    {L"HX - our format (smallest, needs HX to open)", L".hx", L"hx.exe", L"HX Fast or HX Complete", ANY, true},
    {L"ZIP (opens anywhere)", L".zip", L"7z.exe", L"7-Zip, Deflate, maximum", ANY, true},
    {L"7z", L".7z", L"7z.exe", L"7-Zip, LZMA2, -mx9", ANY, true},
    {L"tar.gz", L".tar.gz", L"7z.exe", L"7-Zip, gzip -9", ANY, true},
    {L"tar.xz", L".tar.xz", L"7z.exe", L"7-Zip, xz -9", ANY, true},
    {L"zstd (.zst)", L".zst", L"zstd.exe", L"zstd -19", ANY, false},
    {L"Brotli (.br)", L".br", L"brotli.exe", L"Brotli -q 11", ANY, false},
    {L"kanzi (.knz)", L".knz", L"kanzi.exe", L"kanzi -l 9", ANY, false},
    {L"FLAC - WAV audio only (.flac)", L".flac", L"flac.exe", L"flac -8 -e -p", AUDIO, false},
    {L"WavPack - WAV audio only (.wv)", L".wv", L"wavpack.exe", L"WavPack -hh", AUDIO, false},
    {L"WebP lossless - images (.webp)", L".webp", L"cwebp.exe", L"cwebp -lossless -z 9", IMAGE, false},
    {L"JPEG XL lossless - images, photos, JPEG (.jxl)", L".jxl", L"cjxl.exe", L"cjxl lossless, effort 9", IMAGE, false},
};
static const int NF = sizeof(FMTS) / sizeof(FMTS[0]);

// ---------- globals ----------
static HWND g_wnd, g_list, g_fmt, g_fast, g_complete, g_outdir, g_log, g_prog, g_btnc, g_btnx, g_verify;
static W g_dir, g_tools;
static vector<W> g_files;
static bool g_busy = false;
static int g_dpi = 96; static HFONT g_font;
static int S(int v) { return MulDiv(v, g_dpi, 96); }   // sizes are written for 100% scaling and grown to the screen's setting

static W lower(W s) { for (auto &c : s) c = towlower(c); return s; }
static bool ends(const W &s, const W &e) { W a = lower(s), b = lower(e); return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0; }
static bool exists(const W &p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool isdir(const W &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static W base(const W &p) { size_t k = p.find_last_of(L"\\/"); return k == W::npos ? p : p.substr(k + 1); }
static W parent(const W &p) { size_t k = p.find_last_of(L"\\/"); return k == W::npos ? L"." : p.substr(0, k); }
static W tool(const wchar_t *t) {   // the tools folder first; 7-Zip also from its normal install folder
    W p = g_tools + L"\\" + t; if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    if (!wcscmp(t, L"7z.exe")) for (auto e : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"}) { wchar_t b[MAX_PATH]; if (GetEnvironmentVariableW(e, b, MAX_PATH)) { W s = W(b) + L"\\7-Zip\\7z.exe"; if (GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES) return s; } }
    return p; }
static bool have(const wchar_t *t) { return exists(tool(t)); }
static unsigned long long fsize(const W &p) { WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0; return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow; }
static W q(const W &s) { return L"\"" + s + L"\""; }
static W temp_dir() { wchar_t b[MAX_PATH]; GetTempPathW(MAX_PATH, b); W d = W(b) + L"hxgui_" + to_wstring(GetCurrentProcessId()) + L"_" + to_wstring(GetTickCount()); CreateDirectoryW(d.c_str(), nullptr); return d; }
static void rmtree(const W &d) { W z = d + L'\0' + L'\0'; SHFILEOPSTRUCTW op = {}; op.wFunc = FO_DELETE; op.pFrom = z.c_str(); op.fFlags = FOF_NO_UI; SHFileOperationW(&op); }
static bool is_audio(const W &p) { return ends(p, L".wav"); }
static bool is_image(const W &p) { for (auto e : {L".png", L".jpg", L".jpeg", L".ppm", L".pgm", L".pnm", L".gif", L".tif", L".tiff", L".webp"}) if (ends(p, e)) return true; return false; }

// HX reads plain (narrow) file names; for names with non-English letters we hand it the Windows short name instead.
static W narrow_safe(const W &p, bool create) {
    bool ascii = true; for (auto c : p) if (c > 127) ascii = false; if (ascii) return p;
    if (create && !exists(p)) { HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    wchar_t b[32768]; DWORD n = GetShortPathNameW(p.c_str(), b, 32768); return n ? W(b, n) : p;
}

static void log(const W &s) { W *m = new W(s); PostMessageW(g_wnd, WM_LOGLINE, 0, (LPARAM)m); }

// Runs a program with no window, waits for it, returns its exit code (or -1).
static int run(const W &cmdline, const W &cwd = L"") {
    STARTUPINFOW si = {}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {}; vector<wchar_t> c(cmdline.begin(), cmdline.end()); c.push_back(0);
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return (int)code;
}
static bool same_file(const W &a, const W &b) {
    if (fsize(a) != fsize(b)) return false;
    FILE *x = _wfopen(a.c_str(), L"rb"), *y = _wfopen(b.c_str(), L"rb"); bool ok = x && y;
    vector<char> p(1 << 20), r(1 << 20);
    while (ok) { size_t n = fread(p.data(), 1, p.size(), x), m = fread(r.data(), 1, r.size(), y); if (n != m || memcmp(p.data(), r.data(), n)) ok = false; if (n == 0) break; }
    if (x) fclose(x); if (y) fclose(y); return ok;
}
// Compares two folder trees file by file.
static bool same_tree(const W &a, const W &b) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((a + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return false; bool ok = true;
    do { W n = fd.cFileName; if (n == L"." || n == L"..") continue; W pa = a + L"\\" + n, pb = b + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = isdir(pb) && same_tree(pa, pb); else ok = exists(pb) && same_file(pa, pb);
    } while (ok && FindNextFileW(h, &fd)); FindClose(h); return ok;
}
static W hx_threads() { SYSTEM_INFO s; GetSystemInfo(&s); return to_wstring(max<DWORD>(1, s.dwNumberOfProcessors)); }
static W human(unsigned long long b) { wchar_t t[64]; if (b >= 1ull << 30) swprintf(t, 64, L"%.2f GB", b / 1073741824.0); else if (b >= 1 << 20) swprintf(t, 64, L"%.2f MB", b / 1048576.0); else swprintf(t, 64, L"%llu bytes", b); return t; }

// ---------- compress ----------
struct Job { vector<W> files; int fmt; bool complete, verify; W outdir; };
static unsigned long long tree_size(const W &p) {
    if (!isdir(p)) return fsize(p); unsigned long long t = 0; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((p + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return 0;
    do { W n = fd.cFileName; if (n != L"." && n != L"..") t += tree_size(p + L"\\" + n); } while (FindNextFileW(h, &fd)); FindClose(h); return t;
}
// Puts the chosen files and folders into one tar (7-Zip), used when several items go into a single-file format.
static bool make_tar(const vector<W> &files, const W &tar) {
    W lst = parent(tar) + L"\\list.txt"; FILE *f = _wfopen(lst.c_str(), L"w, ccs=UTF-8"); if (!f) return false;
    for (auto &x : files) fwprintf(f, L"%ls\n", x.c_str()); fclose(f);
    return run(q(tool(L"7z.exe")) + L" a -ttar -spf2 -y " + q(tar) + L" @" + q(lst)) == 0 || run(q(tool(L"7z.exe")) + L" a -ttar -y " + q(tar) + L" @" + q(lst)) == 0;
}
static W unique_out(const W &p) { if (!exists(p)) return p; size_t dot = p.find_last_of(L'.'); W a = p, b; size_t k = p.rfind(L".tar."); if (k != W::npos) dot = k; if (dot != W::npos && dot > p.find_last_of(L"\\/")) { a = p.substr(0, dot); b = p.substr(dot); } for (int i = 2;; i++) { W t = a + L" (" + to_wstring(i) + L")" + b; if (!exists(t)) return t; } }

static bool compress_one(const Job &J, const vector<W> &items, const W &name) {
    const Fmt &F = FMTS[J.fmt]; W out = unique_out(J.outdir + L"\\" + name + F.ext); W td = temp_dir(); bool ok = false; DWORD t0 = GetTickCount();
    unsigned long long in_bytes = 0; for (auto &x : items) in_bytes += tree_size(x);
    log(L"Compressing " + name + L" as " + F.ext + (J.fmt == 0 ? (J.complete ? W(L" (HX Complete)") : W(L" (HX Fast)")) : W(L"")) + L" ...");
    bool single = items.size() == 1 && !isdir(items[0]);
    if (!single && !F.multi) out = unique_out(J.outdir + L"\\" + name + L".tar" + F.ext);   // bundled first, so the name says .tar
    W src = single ? items[0] : td + L"\\data.tar";
    if (!single && !F.multi && !make_tar(items, src)) { log(L"  could not bundle the files (needs 7z.exe in the tools folder)"); rmtree(td); return false; }
    int rc = -1; W z = q(tool(F.tool));
    switch (J.fmt) {
    case 0: {   // HX: one file goes straight in; several items or a folder are bundled into a tar first, then HX
        if (!single && !make_tar(items, src)) { log(L"  could not bundle the files (needs 7z.exe in the tools folder)"); break; }
        W o = out; if (!single && !ends(o, L".tar.hx")) { o = unique_out(J.outdir + L"\\" + name + L".tar.hx"); out = o; }
        rc = run(z + L" c " + q(narrow_safe(src, false)) + L" " + q(narrow_safe(o, true)) + L" " + hx_threads() + (J.complete ? L" --complete --no-learn" : L" --no-learn")); break; }
    case 1: { W args; for (auto &x : items) args += L" " + q(x); rc = run(z + L" a -tzip -mx9 -y " + q(out) + args); break; }
    case 2: { W args; for (auto &x : items) args += L" " + q(x); rc = run(z + L" a -t7z -mx9 -y " + q(out) + args); break; }
    case 3: case 4: { W tar = td + L"\\" + name + L".tar"; if (!make_tar(items, tar)) break; rc = run(z + (J.fmt == 3 ? L" a -tgzip -mx9 -y " : L" a -txz -mx9 -y ") + q(out) + L" " + q(tar)); break; }
    case 5: rc = run(z + L" -19 -f -q " + q(src) + L" -o " + q(out)); break;
    case 6: rc = run(z + L" -q 11 -f -o " + q(out) + L" " + q(src)); break;
    case 7: rc = run(z + L" -c -l 9 -f -i " + q(src) + L" -o " + q(out)); break;
    case 8: rc = run(z + L" -8 -e -p -f -s " + q(src) + L" -o " + q(out)); break;
    case 9: rc = run(z + L" -hh -y -q " + q(src) + L" " + q(out)); break;
    case 10: rc = run(z + L" -lossless -z 9 -quiet " + q(src) + L" -o " + q(out)); break;
    case 11: rc = run(z + L" " + q(src) + L" " + q(out) + L" -d 0 -e 9 --quiet"); break;
    }
    double secs = (GetTickCount() - t0) / 1000.0;
    if (rc != 0 || !exists(out) || fsize(out) == 0) { log(L"  failed (program returned " + to_wstring(rc) + L")"); DeleteFileW(out.c_str()); rmtree(td); return false; }
    unsigned long long ob = fsize(out); wchar_t t[160];
    swprintf(t, 160, L"  done: %ls -> %ls, ratio %.3f, %.1f s", human(in_bytes).c_str(), human(ob).c_str(), ob ? (double)in_bytes / ob : 0.0, secs); log(t);
    ok = true;
    if (J.verify) {   // open the new archive again and compare with the input
        W vd = td + L"\\verify"; CreateDirectoryW(vd.c_str(), nullptr); bool v = false;
        if (J.fmt >= 8) { v = true; log(L"  (FLAC, WavPack, WebP and JPEG XL check their own output)"); }
        else if (J.fmt == 0) { W r = vd + L"\\out.bin"; v = run(z + L" d " + q(narrow_safe(out, false)) + L" " + q(r) + L" " + hx_threads()) == 0 && same_file(src, r); }
        else if (J.fmt >= 5) { W r = vd + L"\\out.bin";
            int c = J.fmt == 5 ? run(z + L" -d -f -q " + q(out) + L" -o " + q(r)) : J.fmt == 6 ? run(z + L" -d -f -o " + q(r) + L" " + q(out)) : run(z + L" -d -f -i " + q(out) + L" -o " + q(r));
            v = c == 0 && same_file(src, r); }
        else { v = run(z + L" t -y " + q(out)) == 0; log(v ? L"  verified: 7-Zip tested the archive and every check sum matches" : L"  WARNING: 7-Zip found a problem - keep your original files"); rmtree(td); return v; }
        log(v ? L"  verified: opened again and matches the original byte for byte" : L"  WARNING: verification failed - keep your original files");
        ok = v;
    }
    rmtree(td); return ok;
}
static void compress_job(Job J) {
    int good = 0, total = 0; const Fmt &F = FMTS[J.fmt];
    if (F.kind != ANY) {   // audio and image formats: one output per file
        for (auto &f : J.files) { total++; if (F.kind == AUDIO && !is_audio(f)) { log(L"Skipped " + base(f) + L" (not a WAV file)"); continue; }
            if (F.kind == IMAGE && !is_image(f)) { log(L"Skipped " + base(f) + L" (not an image)"); continue; }
            W n = base(f); size_t d = n.find_last_of(L'.'); if (d != W::npos) n = n.substr(0, d); good += compress_one(J, {f}, n); }
    } else {
        total = 1; W name = J.files.size() == 1 ? base(J.files[0]) : L"archive";
        good += compress_one(J, J.files, name);
    }
    log(good == total ? L"Finished." : L"Finished with problems - see above.");
    PostMessageW(g_wnd, WM_JOBDONE, 0, 0);
}

// ---------- extract ----------
static void extract_job(W arc, W dest) {
    W td = temp_dir(); bool ok = false; DWORD t0 = GetTickCount(); W name = base(arc);
    log(L"Extracting " + name + L" ...");
    auto strip = [&](const W &e) { return name.substr(0, name.size() - e.size()); };
    auto untar_if = [&](const W &file) {   // a .tar inside is unpacked into the destination
        if (ends(file, L".tar")) { bool r = run(q(tool(L"7z.exe")) + L" x -y -o" + q(dest) + L" " + q(file)) == 0; DeleteFileW(file.c_str()); return r; } return true; };
    W o;
    if (ends(arc, L".hx")) { o = unique_out(dest + L"\\" + strip(L".hx")); ok = run(q(tool(L"hx.exe")) + L" d " + q(narrow_safe(arc, false)) + L" " + q(narrow_safe(o, true)) + L" " + hx_threads()) == 0 && untar_if(o); }
    else if (ends(arc, L".zst")) { o = unique_out(dest + L"\\" + strip(L".zst")); ok = run(q(tool(L"zstd.exe")) + L" -d -f -q " + q(arc) + L" -o " + q(o)) == 0 && untar_if(o); }
    else if (ends(arc, L".br")) { o = unique_out(dest + L"\\" + strip(L".br")); ok = run(q(tool(L"brotli.exe")) + L" -d -f -o " + q(o) + L" " + q(arc)) == 0 && untar_if(o); }
    else if (ends(arc, L".knz")) { o = unique_out(dest + L"\\" + strip(L".knz")); ok = run(q(tool(L"kanzi.exe")) + L" -d -f -i " + q(arc) + L" -o " + q(o)) == 0 && untar_if(o); }
    else if (ends(arc, L".flac")) { o = unique_out(dest + L"\\" + strip(L".flac") + L".wav"); ok = run(q(tool(L"flac.exe")) + L" -d -f -s " + q(arc) + L" -o " + q(o)) == 0; }
    else if (ends(arc, L".wv")) { o = unique_out(dest + L"\\" + strip(L".wv") + L".wav"); ok = run(q(tool(L"wvunpack.exe")) + L" -y -q " + q(arc) + L" " + q(o)) == 0; }
    else if (ends(arc, L".webp")) { o = unique_out(dest + L"\\" + strip(L".webp") + L".png"); ok = run(q(tool(L"dwebp.exe")) + L" -quiet " + q(arc) + L" -o " + q(o)) == 0; }
    else if (ends(arc, L".jxl")) { o = unique_out(dest + L"\\" + strip(L".jxl") + L".png"); ok = run(q(tool(L"djxl.exe")) + L" " + q(arc) + L" " + q(o) + L" --quiet") == 0; }
    else if (ends(arc, L".tar.gz") || ends(arc, L".tgz") || ends(arc, L".tar.xz") || ends(arc, L".txz")) {   // outer layer, then the tar
        ok = run(q(tool(L"7z.exe")) + L" x -y -o" + q(td) + L" " + q(arc)) == 0; W inner;
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((td + L"\\*").c_str(), &fd); if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) inner = td + L"\\" + fd.cFileName; } while (FindNextFileW(h, &fd)); FindClose(h); }
        ok = ok && !inner.empty() && run(q(tool(L"7z.exe")) + L" x -y -o" + q(dest) + L" " + q(inner)) == 0; }
    else ok = run(q(tool(L"7z.exe")) + L" x -y -o" + q(dest) + L" " + q(arc)) == 0;   // zip, 7z, tar, gz, xz, rar and the rest 7-Zip knows
    wchar_t t[120]; swprintf(t, 120, L"  %ls in %.1f s -> %ls", ok ? L"done" : L"FAILED", (GetTickCount() - t0) / 1000.0, dest.c_str()); log(t);
    rmtree(td); log(L"Finished."); PostMessageW(g_wnd, WM_JOBDONE, 0, 0);
}

// ---------- UI ----------
static void refresh_list() {
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (auto &f : g_files) SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)(f + (isdir(f) ? L"   [folder]" : L"   (" + human(fsize(f)) + L")")).c_str());
}
static void refresh_formats() {
    int sel = (int)SendMessageW(g_fmt, CB_GETCURSEL, 0, 0); if (sel < 0) sel = 0;
    SendMessageW(g_fmt, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < NF; i++) { W s = FMTS[i].name; if (!have(FMTS[i].tool)) s += L"   - not installed (tools folder)"; SendMessageW(g_fmt, CB_ADDSTRING, 0, (LPARAM)s.c_str()); }
    SendMessageW(g_fmt, CB_SETCURSEL, sel, 0);
}
static void update_controls() {
    int f = (int)SendMessageW(g_fmt, CB_GETCURSEL, 0, 0); bool hx = f == 0;
    EnableWindow(g_fast, hx && !g_busy); EnableWindow(g_complete, hx && !g_busy);
    bool can = !g_busy && !g_files.empty() && f >= 0 && have(FMTS[f].tool) && (FMTS[f].multi || f == 0 || have(L"7z.exe") || (g_files.size() == 1 && !isdir(g_files[0])));
    if (f >= 0 && FMTS[f].kind == AUDIO) { can = can && any_of(g_files.begin(), g_files.end(), is_audio); }
    if (f >= 0 && FMTS[f].kind == IMAGE) { can = can && any_of(g_files.begin(), g_files.end(), is_image); }
    EnableWindow(g_btnc, can); EnableWindow(g_btnx, !g_busy);
}
static void add_files(const vector<W> &v) { for (auto &x : v) if (find(g_files.begin(), g_files.end(), x) == g_files.end()) g_files.push_back(x);
    if (!g_files.empty()) { wchar_t b[MAX_PATH]; GetWindowTextW(g_outdir, b, MAX_PATH); if (!b[0]) SetWindowTextW(g_outdir, parent(g_files[0]).c_str()); }
    refresh_list(); update_controls(); }
static W pick_folder(HWND h, const wchar_t *title) {
    BROWSEINFOW bi = {}; bi.hwndOwner = h; bi.lpszTitle = title; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi); if (!id) return L""; wchar_t p[MAX_PATH]; SHGetPathFromIDListW(id, p); CoTaskMemFree(id); return p;
}
static HWND mk(const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
static void layout(int W_, int H_) {   // W_, H_ are real pixels; everything else is in 100% units through S()
    auto mv = [](HWND c, int x, int y, int w, int h) { MoveWindow(c, x, y, w, h, TRUE); };
    int m = S(12), bw = S(120), lh = S(150);
    mv(g_list, m, S(34), W_ - 2 * m - bw - S(8), lh);
    mv(GetDlgItem(g_wnd, ID_ADDF), W_ - m - bw, S(34), bw, S(28)); mv(GetDlgItem(g_wnd, ID_ADDD), W_ - m - bw, S(66), bw, S(28)); mv(GetDlgItem(g_wnd, ID_CLEAR), W_ - m - bw, S(98), bw, S(28));
    mv(GetDlgItem(g_wnd, 201), m, S(200), S(80), S(20)); mv(g_fmt, S(96), S(196), W_ - S(96) - m, S(400));
    mv(GetDlgItem(g_wnd, 202), m, S(234), S(80), S(20)); mv(g_fast, S(96), S(232), S(280), S(22)); mv(g_complete, S(386), S(232), S(280), S(22));
    mv(GetDlgItem(g_wnd, 203), m, S(268), S(80), S(20)); mv(g_outdir, S(96), S(264), W_ - S(96) - m - S(98), S(26)); mv(GetDlgItem(g_wnd, ID_OUTDIR), W_ - m - S(90), S(263), S(90), S(28));
    mv(g_btnc, m, S(304), S(150), S(36)); mv(g_btnx, m + S(160), S(304), S(220), S(36)); mv(g_verify, m + S(394), S(311), S(300), S(22));
    mv(g_prog, m, S(350), W_ - 2 * m, S(14));
    mv(g_log, m, S(372), W_ - 2 * m, max(S(60), H_ - S(372) - m));
}
static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = h; { HMODULE u = GetModuleHandleW(L"user32.dll"); auto f = (UINT(WINAPI *)(HWND))GetProcAddress(u, "GetDpiForWindow"); if (f) g_dpi = f(h); else { HDC dc = GetDC(h); g_dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(h, dc); } }
        { NONCLIENTMETRICSW nm = {}; nm.cbSize = sizeof(nm); SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0); nm.lfMessageFont.lfHeight = -MulDiv(10, g_dpi, 72); g_font = CreateFontIndirectW(&nm.lfMessageFont); }
        { RECT r; GetWindowRect(h, &r); SetWindowPos(h, nullptr, 0, 0, S(860), S(680), SWP_NOMOVE | SWP_NOZORDER); }
        mk(L"STATIC", L"Files and folders to compress (or drag them here):", 0, 12, 10, 500, 20, 0);
        g_list = mk(L"LISTBOX", L"", WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | LBS_EXTENDEDSEL, 0, 0, 10, 10, ID_LIST, WS_EX_CLIENTEDGE);
        mk(L"BUTTON", L"Add files...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_ADDF); mk(L"BUTTON", L"Add folder...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_ADDD); mk(L"BUTTON", L"Clear list", BS_PUSHBUTTON, 0, 0, 10, 10, ID_CLEAR);
        mk(L"STATIC", L"Save as:", 0, 12, 199, 76, 18, 201);
        g_fmt = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 10, ID_FMT);
        mk(L"STATIC", L"HX mode:", 0, 12, 230, 76, 18, 202);
        g_fast = mk(L"BUTTON", L"HX Fast - quick, very fast to open", BS_AUTORADIOBUTTON | WS_GROUP, 90, 228, 250, 20, ID_FAST);
        g_complete = mk(L"BUTTON", L"HX Complete - smallest, slower", BS_AUTORADIOBUTTON, 350, 228, 250, 20, ID_COMPLETE);
        SendMessageW(g_fast, BM_SETCHECK, BST_CHECKED, 0);
        mk(L"STATIC", L"Save to:", 0, 12, 261, 76, 18, 203);
        g_outdir = mk(L"EDIT", L"", ES_AUTOHSCROLL, 0, 0, 10, 10, 0, WS_EX_CLIENTEDGE); mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_OUTDIR);
        g_btnc = mk(L"BUTTON", L"Compress", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_COMPRESS);
        g_btnx = mk(L"BUTTON", L"Open archive and extract...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_EXTRACT);
        g_verify = mk(L"BUTTON", L"Check every archive after making it", BS_AUTOCHECKBOX, 0, 0, 10, 10, ID_VERIFY); SendMessageW(g_verify, BM_SETCHECK, BST_CHECKED, 0);
        g_prog = mk(PROGRESS_CLASSW, L"", PBS_MARQUEE, 0, 0, 10, 10, ID_PROG);
        g_log = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_LOG, WS_EX_CLIENTEDGE);
        refresh_formats(); DragAcceptFiles(h, TRUE);
        log(L"HX Compressor 1.0 - Michael Hugh Harrington. HX is Patent Pending (U.S. Provisional App. 64/192,843).");
        log(L"Free for personal use and research; commercial use needs a license. Tools folder: " + g_tools);
        update_controls(); return 0; }
    case WM_SIZE: layout(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize = {S(760), S(560)}; return 0;
    case WM_DPICHANGED: { g_dpi = HIWORD(wp); RECT *r = (RECT *)lp; SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); return 0; }
    case WM_DROPFILES: { HDROP d = (HDROP)wp; UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0); vector<W> v;
        for (UINT i = 0; i < n; i++) { wchar_t p[32768]; DragQueryFileW(d, i, p, 32768); v.push_back(p); } DragFinish(d); if (!g_busy) add_files(v); return 0; }
    case WM_LOGLINE: { W *s = (W *)lp; int len = GetWindowTextLengthW(g_log); SendMessageW(g_log, EM_SETSEL, len, len); SendMessageW(g_log, EM_REPLACESEL, 0, (LPARAM)(*s + L"\r\n").c_str()); delete s; return 0; }
    case WM_JOBDONE: g_busy = false; SendMessageW(g_prog, PBM_SETMARQUEE, FALSE, 0); update_controls(); return 0;
    case WM_COMMAND: switch (LOWORD(wp)) {
        case ID_ADDF: { vector<wchar_t> buf(1 << 16, 0); OPENFILENAMEW o = {}; o.lStructSize = sizeof(o); o.hwndOwner = h; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
            o.lpstrFilter = L"All files\0*.*\0"; o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&o)) { W dir = buf.data(); const wchar_t *p = buf.data() + dir.size() + 1; vector<W> v; if (!*p) v.push_back(dir); else for (; *p; p += wcslen(p) + 1) v.push_back(dir + L"\\" + p); add_files(v); } break; }
        case ID_ADDD: { W f = pick_folder(h, L"Choose a folder to compress"); if (!f.empty()) add_files({f}); break; }
        case ID_CLEAR: g_files.clear(); refresh_list(); update_controls(); break;
        case ID_OUTDIR: { W f = pick_folder(h, L"Save the archive to this folder"); if (!f.empty()) SetWindowTextW(g_outdir, f.c_str()); break; }
        case ID_FMT: if (HIWORD(wp) == CBN_SELCHANGE) update_controls(); break;
        case ID_COMPRESS: { if (g_busy || g_files.empty()) break; Job J; J.files = g_files; J.fmt = (int)SendMessageW(g_fmt, CB_GETCURSEL, 0, 0);
            J.complete = SendMessageW(g_complete, BM_GETCHECK, 0, 0) == BST_CHECKED; J.verify = SendMessageW(g_verify, BM_GETCHECK, 0, 0) == BST_CHECKED;
            wchar_t b[MAX_PATH]; GetWindowTextW(g_outdir, b, MAX_PATH); J.outdir = b[0] ? b : parent(g_files[0]); CreateDirectoryW(J.outdir.c_str(), nullptr);
            if (!isdir(J.outdir)) { MessageBoxW(h, L"The Save to folder does not exist.", L"HX Compressor", MB_ICONWARNING); break; }
            g_busy = true; update_controls(); SendMessageW(g_prog, PBM_SETMARQUEE, TRUE, 30); thread(compress_job, J).detach(); break; }
        case ID_EXTRACT: { if (g_busy) break; wchar_t f[32768] = {}; OPENFILENAMEW o = {}; o.lStructSize = sizeof(o); o.hwndOwner = h; o.lpstrFile = f; o.nMaxFile = 32768;
            o.lpstrFilter = L"Archives\0*.hx;*.zip;*.7z;*.tar;*.gz;*.tgz;*.xz;*.txz;*.zst;*.br;*.knz;*.flac;*.wv;*.webp;*.jxl;*.rar;*.bz2\0All files\0*.*\0"; o.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
            if (!GetOpenFileNameW(&o)) break; W dest = pick_folder(h, L"Extract into this folder"); if (dest.empty()) break;
            g_busy = true; update_controls(); SendMessageW(g_prog, PBM_SETMARQUEE, TRUE, 30); thread(extract_job, W(f), dest).detach(); break; }
        } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int show) {
    INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t me[MAX_PATH]; GetModuleFileNameW(nullptr, me, MAX_PATH); g_dir = parent(me); g_tools = isdir(g_dir + L"\\tools") ? g_dir + L"\\tools" : g_dir;
    WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = hi; wc.lpszClassName = L"HXCompressor"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hIcon = LoadIconW(hi, L"APPICON"); RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"HXCompressor", L"HX Compressor - Patent Pending", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 640, nullptr, nullptr, hi, nullptr);
    ShowWindow(h, show); UpdateWindow(h);
    int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc); vector<W> v; for (int i = 1; i < argc; i++) v.push_back(argv[i]); if (!v.empty()) add_files(v);
    MSG m; while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(h, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    return 0;
}
