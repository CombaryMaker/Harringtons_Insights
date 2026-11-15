// Harrington-3MF Research Encoder - first prototype (15 Nov 2026). Michael Hugh Harrington, MIT License.
// Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.
// A small Windows program: load STL or OBJ files, get standard 3MF files that ordinary slicers open, smaller than usual.
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dbt.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cmath>
#include "s3mf_core.h"
using namespace std;
typedef wstring W;

static const wchar_t* APPNAME = L"Harrington-3MF Research Encoder";
static const wchar_t* FEEDBACK = L"mailto:michaelhh@gmail.com?subject=Harrington-3MF%20feedback";
enum { ID_TAB_ENC = 101, ID_TAB_ABOUT, ID_ADD, ID_REMOVE, ID_CLEAR, ID_LIST, ID_DEST, ID_REFRESH, ID_GO, ID_CANCEL, ID_FEEDBACK, ID_ABOUTTEXT, ID_PROGRESS,
       WM_JOBMSG = WM_APP + 1, WM_JOBDONE };
static HWND g_wnd, g_tabEnc, g_tabAbout, g_add, g_remove, g_clear, g_list, g_destLbl, g_dest, g_refresh, g_hint, g_go, g_cancel, g_feedback, g_status, g_about, g_prog, g_dropLbl;
static HFONT g_font, g_bold, g_title, g_sub; static int g_dpi = 96; static int S(int v) { return MulDiv(v, g_dpi, 96); }
static int g_tab = 0; static RECT g_panel, g_titleRect, g_subRect; static HBRUSH g_panelBrush; static const COLORREF PANEL = RGB(236, 237, 239);
static atomic<bool> g_busy(false), g_stop(false);

struct Item { W path; long long size = 0; W out; long long outSize = 0; W status = L"Waiting"; };
static vector<Item> g_items;
struct Dest { W label, path; bool removable = false; };
static vector<Dest> g_dests;   // 0 = same folder, then drives, last = choose folder
static W g_chosenFolder;

// ------------------------------------------------------------------ helpers
static W fmtn(long long v) { W s = to_wstring(v); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, L","); return s; }
static W human(long long b) { if (b < 1000) return fmtn(b) + L" bytes"; const wchar_t* u[] = {L"KB", L"MB", L"GB", L"TB"}; double v = (double)b; int k = -1;
    while (v >= 1000 && k < 3) { v /= 1000; k++; } wchar_t s[64]; swprintf(s, 64, L"%.1f %s", v, u[k]); return s; }
static W lower(W s) { for (auto& c : s) c = towlower(c); return s; }
static bool is_mesh_file(const W& p) { W l = lower(p); return (l.size() > 4 && (l.substr(l.size() - 4) == L".stl" || l.substr(l.size() - 4) == L".obj")); }
static W folder_of(const W& p) { size_t k = p.find_last_of(L"\\/"); return k == W::npos ? W() : p.substr(0, k + 1); }
static W stem_of(const W& p) { size_t k = p.find_last_of(L"\\/"); W n = k == W::npos ? p : p.substr(k + 1); size_t d = n.find_last_of(L'.'); return d == W::npos ? n : n.substr(0, d); }
static bool exists(const W& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static W unique_name(const W& dir, const W& stem) { W p = dir + stem + L".3mf"; for (int i = 2; exists(p); i++) p = dir + stem + L" (" + to_wstring(i) + L").3mf"; return p; }

// ------------------------------------------------------------------ removable drives (USB sticks, SD cards, USB disks)
static bool usb_bus(wchar_t letter) {
    wchar_t dev[16]; swprintf(dev, 16, L"\\\\.\\%c:", letter);
    HANDLE h = CreateFileW(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL); if (h == INVALID_HANDLE_VALUE) return false;
    STORAGE_PROPERTY_QUERY q = {}; q.PropertyId = StorageDeviceProperty; q.QueryType = PropertyStandardQuery;
    BYTE buf[1024]; DWORD got = 0; bool r = false;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf, sizeof buf, &got, NULL) && got >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        auto* d = (STORAGE_DEVICE_DESCRIPTOR*)buf; r = d->BusType == BusTypeUsb || d->BusType == BusTypeSd || d->BusType == BusTypeMmc || d->RemovableMedia;
    }
    CloseHandle(h); return r;
}
static vector<Dest> find_drives() {
    vector<Dest> v; wchar_t win[MAX_PATH] = {}; GetWindowsDirectoryW(win, MAX_PATH); wchar_t sys = towupper(win[0]); DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue; wchar_t L = (wchar_t)(L'A' + i); if (L == sys) continue;
        W root = W(1, L) + L":\\"; UINT t = GetDriveTypeW(root.c_str());
        bool rem = t == DRIVE_REMOVABLE || (t == DRIVE_FIXED && usb_bus(L)); if (!rem) continue;
        ULARGE_INTEGER fr{}; if (!GetDiskFreeSpaceExW(root.c_str(), &fr, NULL, NULL)) continue;   // empty card reader slot: skip
        wchar_t name[MAX_PATH] = {}; GetVolumeInformationW(root.c_str(), name, MAX_PATH, NULL, NULL, NULL, NULL, 0);
        W label = L"Removable drive " + W(1, L) + L":" + (name[0] ? W(L"  \"") + name + L"\"" : W()) + L"  (" + human((long long)fr.QuadPart) + L" free)";
        v.push_back({label, root, true});
    }
    return v;
}
static void fill_dests(int selectDriveLetter = 0) {
    W keep; int cur = (int)SendMessageW(g_dest, CB_GETCURSEL, 0, 0); if (cur >= 0 && cur < (int)g_dests.size()) keep = g_dests[cur].path;
    g_dests.clear(); g_dests.push_back({L"Same folder as each original file", L"", false});
    for (auto& d : find_drives()) g_dests.push_back(d);
    if (!g_chosenFolder.empty()) g_dests.push_back({L"Folder: " + g_chosenFolder, g_chosenFolder, false});
    g_dests.push_back({L"Choose a folder...", L"?", false});
    SendMessageW(g_dest, CB_RESETCONTENT, 0, 0); for (auto& d : g_dests) SendMessageW(g_dest, CB_ADDSTRING, 0, (LPARAM)d.label.c_str());
    int sel = 0;
    for (int i = 0; i < (int)g_dests.size(); i++) {
        if (selectDriveLetter && g_dests[i].removable && towupper(g_dests[i].path[0]) == towupper((wchar_t)selectDriveLetter)) { sel = i; break; }
        if (!selectDriveLetter && !keep.empty() && g_dests[i].path == keep) sel = i;
    }
    SendMessageW(g_dest, CB_SETCURSEL, sel, 0);
    int n = 0; for (auto& d : g_dests) n += d.removable;
    SetWindowTextW(g_hint, n ? (to_wstring(n) + L" removable drive" + (n > 1 ? L"s" : L"") + L" found. Plug one in at any time - it shows up here on its own.").c_str()
                             : L"No removable drive found. Plug in a USB stick or SD card at any time - it shows up here on its own.");
}

// ------------------------------------------------------------------ list
static void list_refresh() {
    ListView_DeleteAllItems(g_list);
    for (int i = 0; i < (int)g_items.size(); i++) {
        auto& it = g_items[i]; size_t k = it.path.find_last_of(L"\\/"); W name = k == W::npos ? it.path : it.path.substr(k + 1);
        LVITEMW lv = {}; lv.mask = LVIF_TEXT; lv.iItem = i; lv.pszText = (LPWSTR)name.c_str(); ListView_InsertItem(g_list, &lv);
        W a = human(it.size), b = it.outSize ? human(it.outSize) : W(), c;
        if (it.outSize && it.size) { wchar_t s[32]; swprintf(s, 32, L"%.0f%%", 100.0 * (1.0 - (double)it.outSize / it.size)); c = s; }
        ListView_SetItemText(g_list, i, 1, (LPWSTR)a.c_str()); ListView_SetItemText(g_list, i, 2, (LPWSTR)b.c_str());
        ListView_SetItemText(g_list, i, 3, (LPWSTR)c.c_str()); ListView_SetItemText(g_list, i, 4, (LPWSTR)it.status.c_str());
    }
}
static void add_path(const W& p) {
    DWORD a = GetFileAttributesW(p.c_str()); if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_DIRECTORY) {   // a folder: take the STL / OBJ files inside it
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((p + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && is_mesh_file(fd.cFileName)) add_path(p + L"\\" + fd.cFileName); } while (FindNextFileW(h, &fd)); FindClose(h); return;
    }
    if (!is_mesh_file(p)) return; for (auto& it : g_items) if (lower(it.path) == lower(p)) return;
    WIN32_FILE_ATTRIBUTE_DATA d; GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d);
    Item it; it.path = p; it.size = ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow; g_items.push_back(it);
}
static void set_status(const W& s) { SetWindowTextW(g_status, s.c_str()); }
static void update_buttons() {
    bool b = g_busy; EnableWindow(g_go, !b && !g_items.empty()); EnableWindow(g_cancel, b); EnableWindow(g_add, !b); EnableWindow(g_remove, !b); EnableWindow(g_clear, !b); EnableWindow(g_dest, !b); EnableWindow(g_refresh, !b);
}

// ------------------------------------------------------------------ the work (runs on its own thread)
struct Msg { int index; W status; long long outSize; W out; double frac; };
static void post(const Msg& m) { PostMessageW(g_wnd, WM_JOBMSG, 0, (LPARAM) new Msg(m)); }
static void worker(vector<Item> items, W dest) {
    int done = 0, failed = 0; long long inSum = 0, outSum = 0;
    for (int i = 0; i < (int)items.size() && !g_stop; i++) {
        auto& it = items[i]; post({i, L"Working...", 0, W(), (double)i / items.size()});
        HANDLE h = CreateFileW(it.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (h == INVALID_HANDLE_VALUE) { post({i, L"Could not open the file", 0, W(), -1}); failed++; continue; }
        LARGE_INTEGER sz; GetFileSizeEx(h, &sz); vector<uint8_t> buf((size_t)sz.QuadPart); DWORD got = 0; size_t off = 0; bool ok = true;
        while (off < buf.size()) { DWORD want = (DWORD)min<size_t>(buf.size() - off, 1 << 30); if (!ReadFile(h, buf.data() + off, want, &got, NULL) || !got) { ok = false; break; } off += got; }
        CloseHandle(h); if (!ok) { post({i, L"Could not read the file", 0, W(), -1}); failed++; continue; }
        vector<uint8_t> out; W lp = lower(it.path); bool obj = lp.size() > 4 && lp.substr(lp.size() - 4) == L".obj";
        auto r = s3mf::encode(buf, obj, out, [&](const char* stage, double f) {
            W s; for (const char* c = stage; *c; c++) s += (wchar_t)*c; post({i, s + L"...", 0, W(), ((double)i + f) / items.size()}); });
        if (!r.ok) { W e; for (char c : r.error) e += (wchar_t)c; post({i, L"Not converted: " + e, 0, W(), -1}); failed++; continue; }
        W dir = dest.empty() ? folder_of(it.path) : dest; if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
        W outPath = unique_name(dir, stem_of(it.path));
        HANDLE o = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL); DWORD wr = 0;
        bool wok = o != INVALID_HANDLE_VALUE && WriteFile(o, out.data(), (DWORD)out.size(), &wr, NULL) && wr == out.size(); if (o != INVALID_HANDLE_VALUE) { FlushFileBuffers(o); CloseHandle(o); }
        if (!wok) { DeleteFileW(outPath.c_str()); post({i, L"Could not save (drive full or write-protected?)", 0, W(), -1}); failed++; continue; }
        W st = L"Done, checked exact"; if (r.dropped) st += L" (" + fmtn((long long)r.dropped) + L" flat triangles left out, 3MF does not allow them)";
        post({i, st, (long long)out.size(), outPath, -1}); done++; inSum += it.size; outSum += (long long)out.size();
    }
    W s = g_stop ? L"Stopped. " : L"Finished. ";
    s += fmtn(done) + L" file" + (done == 1 ? L"" : L"s") + L" converted";
    if (done && inSum) { wchar_t b[96]; swprintf(b, 96, L", %s in, %s out (%.0f%% smaller than the originals)", human(inSum).c_str(), human(outSum).c_str(), 100.0 * (1.0 - (double)outSum / inSum)); s += b; }
    if (failed) s += L", " + fmtn(failed) + L" not converted"; s += L".";
    PostMessageW(g_wnd, WM_JOBDONE, 0, (LPARAM) new W(s));
}
static void start() {
    if (g_items.empty() || g_busy) return;
    int sel = (int)SendMessageW(g_dest, CB_GETCURSEL, 0, 0); W dest = sel >= 0 && sel < (int)g_dests.size() ? g_dests[sel].path : W();
    if (dest == L"?") { set_status(L"Please choose where to save the 3MF files first."); return; }
    if (!dest.empty() && GetFileAttributesW(dest.c_str()) == INVALID_FILE_ATTRIBUTES) { set_status(L"That drive or folder is not available any more. Please choose again."); fill_dests(); return; }
    for (auto& it : g_items) { it.status = L"Waiting"; it.outSize = 0; } list_refresh();
    g_busy = true; g_stop = false; update_buttons(); SendMessageW(g_prog, PBM_SETPOS, 0, 0); set_status(L"Working - the packing step can take a while on big files.");
    thread(worker, g_items, dest).detach();
}
static void choose_folder() {
    IFileOpenDialog* d = nullptr; W picked;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM); d->SetTitle(L"Where should the 3MF files go?");
        if (SUCCEEDED(d->Show(g_wnd))) { IShellItem* si; if (SUCCEEDED(d->GetResult(&si))) { PWSTR p; if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &p))) { picked = p; CoTaskMemFree(p); } si->Release(); } }
        d->Release();
    }
    if (!picked.empty()) { if (picked.back() != L'\\') picked += L'\\'; g_chosenFolder = picked; }
    fill_dests(); if (!picked.empty()) for (int i = 0; i < (int)g_dests.size(); i++) if (g_dests[i].path == picked) SendMessageW(g_dest, CB_SETCURSEL, i, 0);
}
static void add_files_dialog() {
    vector<wchar_t> buf(1 << 16, 0); OPENFILENAMEW of = {sizeof of}; of.hwndOwner = g_wnd; of.lpstrFilter = L"3D mesh files (*.stl, *.obj)\0*.stl;*.obj\0All files\0*.*\0";
    of.lpstrFile = buf.data(); of.nMaxFile = (DWORD)buf.size(); of.lpstrTitle = L"Choose STL or OBJ files"; of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&of)) return;
    W first = buf.data(); const wchar_t* p = buf.data() + first.size() + 1;
    if (!*p) add_path(first); else { W dir = first; if (dir.back() != L'\\') dir += L'\\'; while (*p) { add_path(dir + p); p += wcslen(p) + 1; } }
    list_refresh(); update_buttons(); set_status(fmtn((long long)g_items.size()) + L" file(s) in the list. Choose where to save them, then press Make 3MF files.");
}

// ------------------------------------------------------------------ painting: dark-to-light grey with a soft metallic glint, built once per size
static HDC g_bgDC = NULL; static HBITMAP g_bgBmp = NULL; static HGDIOBJ g_bgOld = NULL; static int g_bgW = 0, g_bgH = 0;
static void build_bg(int w, int h) {
    if (g_bgDC) { SelectObject(g_bgDC, g_bgOld); DeleteObject(g_bgBmp); DeleteDC(g_bgDC); g_bgDC = NULL; }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = NULL; g_bgBmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0); if (!g_bgBmp || !bits) { g_bgW = g_bgH = 0; return; }
    uint32_t* px = (uint32_t*)bits; double diag = sqrt((double)w * w + (double)h * h);
    static float glint[2048]; static bool made = false;   // glint brightness by distance from the band, worked out once
    if (!made) { for (int i = 0; i < 2048; i++) { double d = (i - 1024) / 1024.0; glint[i] = (float)(22 * exp(-d * d * 260) + 6 * exp(-d * d * 30)); } made = true; }
    for (int y = 0; y < h; y++) {
        double ty = 0.65 * y / max(1, h);
        for (int x = 0; x < w; x++) {
            double v = 62 + (206 - 62) * (0.35 * x / max(1, w) + ty);           // dark grey at the top left, light grey at the bottom right
            double d = ((double)x - (double)y * 1.6 - w * 0.25) / diag;        // one soft diagonal glint band
            int k = (int)(d * 1024) + 1024; if (k >= 0 && k < 2048) v += glint[k];
            int g = (int)min(255.0, v); int b = min(255, g + 3);
            px[(size_t)y * w + x] = ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    g_bgDC = CreateCompatibleDC(NULL); g_bgOld = SelectObject(g_bgDC, g_bgBmp); g_bgW = w; g_bgH = h;
}
static void paint(HDC dc, RECT rc) {
    if (!g_bgDC || rc.right != g_bgW || rc.bottom != g_bgH) build_bg(rc.right, rc.bottom);
    if (g_bgDC) BitBlt(dc, 0, 0, rc.right, rc.bottom, g_bgDC, 0, 0, SRCCOPY);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, g_title); SetTextColor(dc, RGB(245, 246, 248)); DrawTextW(dc, L"Harrington-3MF", -1, &g_titleRect, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    SelectObject(dc, g_sub); SetTextColor(dc, RGB(205, 208, 214)); DrawTextW(dc, L"3MF Research Encoder  -  first prototype", -1, &g_subRect, DT_LEFT | DT_TOP | DT_SINGLELINE); SelectObject(dc, of);
    for (int k = S(6); k >= 1; k--) { int v = 70 + 18 * k; HBRUSH b = CreateSolidBrush(RGB(v, v, v + 2)); RECT r = {g_panel.left + k, g_panel.top + k, g_panel.right + k, g_panel.bottom + k}; FillRect(dc, &r, b); DeleteObject(b); }
    FillRect(dc, &g_panel, g_panelBrush);
}
static void draw_tab(DRAWITEMSTRUCT* di, bool active) {
    RECT r = di->rcItem; HDC dc = di->hDC; COLORREF c = active ? PANEL : RGB(150, 153, 158);
    HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b);
    if (!active) { HPEN p = CreatePen(PS_SOLID, 1, RGB(120, 122, 126)); HGDIOBJ op = SelectObject(dc, p); MoveToEx(dc, r.left, r.bottom - 1, NULL); LineTo(dc, r.right, r.bottom - 1); SelectObject(dc, op); DeleteObject(p); }
    wchar_t t[64]; GetWindowTextW(di->hwndItem, t, 64); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, active ? RGB(25, 27, 30) : RGB(35, 37, 40));
    HGDIOBJ of = SelectObject(dc, active ? g_bold : g_font); DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of);
    if (di->itemState & ODS_FOCUS) { RECT f = r; InflateRect(&f, -S(4), -S(4)); DrawFocusRect(dc, &f); }
}

// ------------------------------------------------------------------ layout
static const wchar_t* ABOUT_TEXT =
    L"Harrington-3MF Research Encoder - first prototype\r\n\r\n"
    L"What it does\r\n"
    L"It turns your STL or OBJ files into 3MF files. A 3MF file is the normal format that 3D printing software already opens - "
    L"Cura, PrusaSlicer, Bambu Studio, OrcaSlicer and others. Nothing extra needs to be installed to open them.\r\n\r\n"
    L"What we found so far\r\n"
    L"On 20 real printer files, the 3MF files made here were about 40 to 45 percent smaller than ordinary 3MF files, "
    L"and about 85 percent smaller than the STL files. Every point of every shape is kept exactly. "
    L"Each file is checked after it is made.\r\n\r\n"
    L"Good to know\r\n"
    L"  - Your original files are never changed or deleted.\r\n"
    L"  - Flat triangles with no area are left out, because the 3MF format does not allow them. They do not print.\r\n"
    L"  - Very large files take a while: the program spends extra time to make them small.\r\n\r\n"
    L"This is a research prototype. If something does not work in your slicer, or you have an idea, please press Send feedback.\r\n\r\n"
    L"Made by Michael Hugh Harrington. MIT License.\r\n"
    L"Uses zopfli (Google, Apache 2.0), miniz (MIT) and meshoptimizer (Arseny Kapoulkine, MIT).";
static void show_tab(int t) {
    g_tab = t; int enc = t == 0 ? SW_SHOW : SW_HIDE, ab = t == 1 ? SW_SHOW : SW_HIDE;
    for (HWND h : {g_add, g_remove, g_clear, g_list, g_destLbl, g_dest, g_refresh, g_hint, g_go, g_cancel, g_prog, g_status, g_dropLbl}) ShowWindow(h, enc);
    ShowWindow(g_about, ab); InvalidateRect(g_tabEnc, NULL, TRUE); InvalidateRect(g_tabAbout, NULL, TRUE);
}
static void layout(int w, int h) {
    int m = S(18), top = S(86);
    g_titleRect = {m, S(8), w - m, S(52)}; g_subRect = {m + S(2), S(52), w - m, S(76)};
    MoveWindow(g_tabEnc, m, top, S(130), S(32), TRUE); MoveWindow(g_tabAbout, m + S(132), top, S(130), S(32), TRUE);
    MoveWindow(g_feedback, w - m - S(150), top, S(150), S(30), TRUE);
    g_panel = {m, top + S(32), w - m, h - m};
    int x = g_panel.left + S(16), r = g_panel.right - S(16), y = g_panel.top + S(16);
    MoveWindow(g_add, x, y, S(190), S(32), TRUE); MoveWindow(g_remove, x + S(198), y, S(140), S(32), TRUE); MoveWindow(g_clear, x + S(346), y, S(110), S(32), TRUE);
    MoveWindow(g_dropLbl, x + S(470), y + S(7), max(0, r - x - S(470)), S(22), TRUE);
    int bottom = g_panel.bottom - S(16);
    int listTop = y + S(42), listBottom = bottom - S(150);
    MoveWindow(g_list, x, listTop, r - x, max(S(60), listBottom - listTop), TRUE);
    int yy = listBottom + S(14);
    MoveWindow(g_destLbl, x, yy + S(5), S(190), S(22), TRUE); MoveWindow(g_dest, x + S(196), yy, max(S(200), r - x - S(196) - S(150)), S(300), TRUE); MoveWindow(g_refresh, r - S(140), yy - S(1), S(140), S(30), TRUE);
    MoveWindow(g_hint, x + S(196), yy + S(32), r - x - S(196), S(22), TRUE);
    yy += S(64); MoveWindow(g_go, x, yy, S(190), S(38), TRUE); MoveWindow(g_cancel, x + S(198), yy + S(4), S(100), S(30), TRUE); MoveWindow(g_prog, x + S(310), yy + S(10), r - x - S(310), S(18), TRUE);
    MoveWindow(g_status, x, yy + S(46), r - x, S(22), TRUE);
    MoveWindow(g_about, x, y, r - x, bottom - y, TRUE);
    int cw = r - x - S(4); int c0 = cw - S(110) * 3 - S(300); if (c0 < S(160)) c0 = S(160);
    int widths[5] = {c0, S(110), S(110), S(110), S(300)}; for (int i = 0; i < 5; i++) ListView_SetColumnWidth(g_list, i, widths[i]);
}
static HWND mk(HWND p, const wchar_t* cls, const wchar_t* text, DWORD style, int id, HFONT f = NULL, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : g_font), TRUE); return h;
}
static void make_fonts() {
    auto F = [](int pt, int wgt) { return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, wgt, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
    g_font = F(10, FW_NORMAL); g_bold = F(10, FW_SEMIBOLD); g_title = F(24, FW_SEMIBOLD); g_sub = F(12, FW_NORMAL);
}

static LRESULT CALLBACK wnd(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_wnd = hw; g_dpi = GetDpiForWindow(hw); make_fonts(); g_panelBrush = CreateSolidBrush(PANEL);
        g_tabEnc = mk(hw, L"BUTTON", L"Encoder", BS_OWNERDRAW | WS_TABSTOP, ID_TAB_ENC); g_tabAbout = mk(hw, L"BUTTON", L"About", BS_OWNERDRAW | WS_TABSTOP, ID_TAB_ABOUT);
        g_feedback = mk(hw, L"BUTTON", L"Send feedback", BS_PUSHBUTTON | WS_TABSTOP, ID_FEEDBACK);
        g_add = mk(hw, L"BUTTON", L"Add STL / OBJ files...", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD, g_bold);
        g_remove = mk(hw, L"BUTTON", L"Remove selected", BS_PUSHBUTTON | WS_TABSTOP, ID_REMOVE); g_clear = mk(hw, L"BUTTON", L"Clear list", BS_PUSHBUTTON | WS_TABSTOP, ID_CLEAR);
        g_dropLbl = mk(hw, L"STATIC", L"or drag files or folders onto this window", SS_LEFT, 0);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOSORTHEADER | LVS_SHOWSELALWAYS, 0, 0, 10, 10, hw, (HMENU)ID_LIST, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE); ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* cols[5] = {L"File", L"Original", L"3MF", L"Smaller by", L"Status"};
        for (int i = 0; i < 5; i++) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = 100; ListView_InsertColumn(g_list, i, &c); }
        g_destLbl = mk(hw, L"STATIC", L"Save the 3MF files to:", SS_LEFT, 0, g_bold);
        g_dest = mk(hw, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_DEST);
        g_refresh = mk(hw, L"BUTTON", L"Look for drives", BS_PUSHBUTTON | WS_TABSTOP, ID_REFRESH);
        g_hint = mk(hw, L"STATIC", L"", SS_LEFT, 0);
        g_go = mk(hw, L"BUTTON", L"Make 3MF files", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_GO, g_bold); g_cancel = mk(hw, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, ID_CANCEL);
        g_prog = mk(hw, PROGRESS_CLASSW, L"", 0, ID_PROGRESS); SendMessageW(g_prog, PBM_SETRANGE32, 0, 1000);
        g_status = mk(hw, L"STATIC", L"Add STL or OBJ files to start.", SS_LEFT | SS_ENDELLIPSIS, 0);
        g_about = mk(hw, L"EDIT", ABOUT_TEXT, ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, ID_ABOUTTEXT);
        DragAcceptFiles(hw, TRUE); fill_dests(); update_buttons(); show_tab(0); return 0; }
    case WM_SIZE: layout(LOWORD(lp), HIWORD(lp)); InvalidateRect(hw, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: { auto* mi = (MINMAXINFO*)lp; mi->ptMinTrackSize.x = S(820); mi->ptMinTrackSize.y = S(600); return 0; }
    case WM_DPICHANGED: { g_dpi = HIWORD(wp); RECT* r = (RECT*)lp; SetWindowPos(hw, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(hw, &ps); RECT rc; GetClientRect(hw, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bm);
        paint(mem, rc); BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem); EndPaint(hw, &ps); return 0; }
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)wp; SetBkColor(dc, PANEL); SetTextColor(dc, RGB(30, 32, 36)); return (LRESULT)g_panelBrush; }
    case WM_DRAWITEM: { auto* di = (DRAWITEMSTRUCT*)lp; if (di->CtlID == ID_TAB_ENC) { draw_tab(di, g_tab == 0); return TRUE; } if (di->CtlID == ID_TAB_ABOUT) { draw_tab(di, g_tab == 1); return TRUE; } break; }
    case WM_DROPFILES: { HDROP d = (HDROP)wp; if (!g_busy) { UINT n = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0); for (UINT i = 0; i < n; i++) { wchar_t p[MAX_PATH * 4]; DragQueryFileW(d, i, p, MAX_PATH * 4); add_path(p); }
        list_refresh(); update_buttons(); show_tab(0); set_status(fmtn((long long)g_items.size()) + L" file(s) in the list. Choose where to save them, then press Make 3MF files."); }
        DragFinish(d); return 0; }
    case WM_DEVICECHANGE: {
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            auto* h = (DEV_BROADCAST_HDR*)lp; int letter = 0;
            if (h && h->dbch_devicetype == DBT_DEVTYP_VOLUME) { DWORD um = ((DEV_BROADCAST_VOLUME*)lp)->dbcv_unitmask; for (int i = 0; i < 26; i++) if (um & (1u << i)) { letter = L'A' + i; break; } }
            if (!g_busy) {
                if (wp == DBT_DEVICEARRIVAL && letter) { fill_dests(letter); set_status(W(L"Drive ") + (wchar_t)letter + L": was plugged in and is now selected as the save place."); }
                else { fill_dests(); if (wp == DBT_DEVICEREMOVECOMPLETE && letter) set_status(W(L"Drive ") + (wchar_t)letter + L": was removed."); }
            }
        }
        return TRUE; }
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_TAB_ENC) show_tab(0); else if (id == ID_TAB_ABOUT) show_tab(1);
        else if (id == ID_FEEDBACK) ShellExecuteW(hw, L"open", FEEDBACK, NULL, NULL, SW_SHOWNORMAL);
        else if (id == ID_ADD) add_files_dialog();
        else if (id == ID_CLEAR) { g_items.clear(); list_refresh(); update_buttons(); set_status(L"Add STL or OBJ files to start."); }
        else if (id == ID_REMOVE) { for (int i = (int)g_items.size() - 1; i >= 0; i--) if (ListView_GetItemState(g_list, i, LVIS_SELECTED)) g_items.erase(g_items.begin() + i); list_refresh(); update_buttons(); }
        else if (id == ID_REFRESH) { fill_dests(); set_status(L"Drive list updated."); }
        else if (id == ID_DEST && code == CBN_SELCHANGE) { int s = (int)SendMessageW(g_dest, CB_GETCURSEL, 0, 0); if (s >= 0 && s < (int)g_dests.size() && g_dests[s].path == L"?") choose_folder(); }
        else if (id == ID_GO) start();
        else if (id == ID_CANCEL) { g_stop = true; set_status(L"Stopping after the current file..."); }
        return 0; }
    case WM_JOBMSG: { Msg* m = (Msg*)lp;
        if (m->index >= 0 && m->index < (int)g_items.size()) { auto& it = g_items[m->index]; it.status = m->status; if (m->outSize) { it.outSize = m->outSize; it.out = m->out; }
            W a = it.outSize ? human(it.outSize) : W(), c; if (it.outSize && it.size) { wchar_t s[32]; swprintf(s, 32, L"%.0f%%", 100.0 * (1.0 - (double)it.outSize / it.size)); c = s; }
            ListView_SetItemText(g_list, m->index, 2, (LPWSTR)a.c_str()); ListView_SetItemText(g_list, m->index, 3, (LPWSTR)c.c_str()); ListView_SetItemText(g_list, m->index, 4, (LPWSTR)it.status.c_str()); }
        if (m->frac >= 0) SendMessageW(g_prog, PBM_SETPOS, (WPARAM)(m->frac * 1000), 0);
        delete m; return 0; }
    case WM_JOBDONE: { W* s = (W*)lp; g_busy = false; SendMessageW(g_prog, PBM_SETPOS, 1000, 0); set_status(*s); delete s; update_buttons(); return 0; }
    case WM_CLOSE: if (g_busy) { g_stop = true; } DestroyWindow(hw); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSW wc = {}; wc.lpfnWndProc = wnd; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"Harrington3MF"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    UINT dpi = GetDpiForSystem(); int w = MulDiv(980, dpi, 96), h = MulDiv(680, dpi, 96);
    HWND hw = CreateWindowExW(0, wc.lpszClassName, APPNAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, hi, NULL);
    ShowWindow(hw, show); UpdateWindow(hw);
    MSG m; while (GetMessageW(&m, NULL, 0, 0)) { if (!IsDialogMessageW(hw, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    CoUninitialize(); return 0;
}
