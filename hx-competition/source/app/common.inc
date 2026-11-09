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
static Run run_cmd(const W &cmdline, bool debugEnv, const W &stdoutFile = W()) {
    Run r; SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    W logf = g_tmp + L"child-output.txt"; DeleteFileW(logf.c_str());
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    HANDLE out = CreateFileW(logf.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    HANDLE sout = INVALID_HANDLE_VALUE; if (!stdoutFile.empty()) sout = CreateFileW(stdoutFile.c_str(), GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = {sizeof si}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = sout != INVALID_HANDLE_VALUE ? sout : out; si.hStdError = out;
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
    if (sout != INVALID_HANDLE_VALUE) CloseHandle(sout); if (!stdoutFile.empty() && sout == INVALID_HANDLE_VALUE) r.ok = false;
    if (job) CloseHandle(job); if (out != INVALID_HANDLE_VALUE) CloseHandle(out); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    FILE *fp = _wfopen(logf.c_str(), L"rb"); if (fp) { char b[4096]; size_t m; while ((m = fread(b, 1, sizeof b, fp)) > 0 && r.out.size() < 200000) r.out.append(b, m); fclose(fp); }
    DeleteFileW(logf.c_str()); return r;
}
static W q(const W &s) { return L"\"" + s + L"\""; }

// ------------------------------------------------------------------ machine description (measured / reported by Windows)
static W machine_info() {
    W cpu = L"unknown CPU"; HKEY k; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t b[256]; DWORD sz = sizeof b; if (RegQueryValueExW(k, L"ProcessorNameString", NULL, NULL, (LPBYTE)b, &sz) == ERROR_SUCCESS) { cpu = b; while (!cpu.empty() && cpu.back() == L' ') cpu.pop_back(); } RegCloseKey(k); }
    MEMORYSTATUSEX ms = {sizeof ms}; GlobalMemoryStatusEx(&ms);
    typedef LONG (WINAPI *RGV)(PRTL_OSVERSIONINFOW); RTL_OSVERSIONINFOW vi = {sizeof vi}; RGV f = (RGV)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); if (f) f(&vi);
    return cpu + L", " + to_wstring(cores()) + L" logical cores, " + fmtd(ms.ullTotalPhys / 1e9, 1) + L" GB RAM, Windows " + to_wstring(vi.dwMajorVersion) + L"." + to_wstring(vi.dwMinorVersion) + L" build " + to_wstring(vi.dwBuildNumber);
}

// Michael Harrington's best versions inside HX, the same in every run (none of them is switched by the stage buttons):
//   HX Fast and Complete: zstd-hi settings for the repeat line (built into hx.exe)
//   HX Complete: lzma-hi settings with record delta, Brotli-hi settings (built into hx.exe), HCM v3.6.1 max (HCM.exe),
//   and the format specialists for images, JPEG files and audio (outside programs below)
struct Extra { const wchar_t *env, *exe, *name; bool flag; };
static const Extra EXTRAS[] = {
    {L"HX_HCM", L"HCM.exe", L"HCM v3.6.1 max (Michael Harrington, MIT) - context-mixing route", false},
    {L"HX_CJXL", L"cjxl.exe", L"JPEG XL 0.12.0 encoder (libjxl, BSD) - 16-bit images, raw images, JPEG repack", false},
    {L"HX_DJXL", L"djxl.exe", L"JPEG XL 0.12.0 decoder (libjxl, BSD)", false},
    {L"HX_CWEBP", L"cwebp.exe", L"WebP 1.6.0 lossless encoder (libwebp, BSD) - colour raw images", false},
    {L"HX_DWEBP", L"dwebp.exe", L"WebP 1.6.0 decoder (libwebp, BSD)", false},
    {L"HX_PACKJPG", L"packJPG.exe", L"packJPG 2.5k (Matthias Stirner, LGPL) - JPEG files", false},
    {L"HX_WAVPACK", L"wavpack.exe", L"WavPack 5.9.0 encoder (David Bryant, BSD) - WAV audio", false},
    {L"HX_WVUNPACK", L"wvunpack.exe", L"WavPack 5.9.0 decoder (David Bryant, BSD)", false}};
static void set_extra_env() {   // child processes (hx.exe) inherit these; a program that is missing is simply not used
    for (auto &e : EXTRAS) { W f = g_tools + e.exe; if (fsize(f) > 0) SetEnvironmentVariableW(e.env, f.c_str()); else SetEnvironmentVariableW(e.env, NULL); }
    SetEnvironmentVariableW(L"HX_MEDIA", L"1");
}
static W extras_text(bool complete, bool adv) {
    W s = L"Michael Harrington's best versions inside HX (same in every run, not switched by the stage buttons):\r\n";
    s += L"  zstd-hi settings for the repeat line (in hx.exe)\r\n";
    if (!complete) return s;
    s += L"  lzma-hi settings with record delta, Brotli-hi settings (in hx.exe)\r\n";
    { W f = g_tools + L"hdeflate.exe"; long long z = fsize(f); s += W(L"  H-Deflate v1.01 (Michael Harrington, MIT) - unpacks the deflate streams in PNG, ZIP/Office, PDF and gzip files before HX") +
        (z > 0 ? (adv ? L", " + fmtn(z) + L" bytes, SHA-256 " + sha_file(f) : W()) : W(L" - NOT INSTALLED, not used")) + L"\r\n"; }
    for (auto &e : EXTRAS) { W f = g_tools + e.exe; long long z = fsize(f);
        s += L"  " + W(e.name) + (z > 0 ? (adv ? L", " + fmtn(z) + L" bytes, SHA-256 " + sha_file(f) : W()) : W(L" - NOT INSTALLED, not used")) + L"\r\n"; }
    return s;
}
static bool deflate_kind(const W &f) {   // PNG, ZIP (DOCX/XLSX/PPTX), gzip, PDF, OLE (DOC/XLS/PPT with embedded zlib pictures)
    FILE *h = _wfopen(f.c_str(), L"rb"); if (!h) return false; unsigned char b[8] = {0}; size_t n = fread(b, 1, 8, h); fclose(h); if (n < 4) return false;
    return (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') || (b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4) || (b[0] == 0x1F && b[1] == 0x8B) ||
           (b[0] == '%' && b[1] == 'P' && b[2] == 'D' && b[3] == 'F') || (b[0] == 0xD0 && b[1] == 0xCF && b[2] == 0x11 && b[3] == 0xE0);
}
// ------------------------------------------------------------------ painting: soft grey texture, shadowed panels
// The textured background is built once in memory (direct pixel access) and only rebuilt when the window size changes.
static HDC g_bgDC = NULL; static HBITMAP g_bgBmp = NULL; static HGDIOBJ g_bgOld = NULL; static int g_bgW = 0, g_bgH = 0;
static void build_bg(int w, int h) {
    if (g_bgDC) { SelectObject(g_bgDC, g_bgOld); DeleteObject(g_bgBmp); DeleteDC(g_bgDC); g_bgDC = NULL; }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB; void *bits = NULL;
    g_bgBmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0); if (!g_bgBmp || !bits) { g_bgW = g_bgH = 0; return; }
    uint32_t *px = (uint32_t *)bits;
    for (int y = 0; y < h; y++) { int yy = y & ~1; int v = 214 - 14 * yy / max(1, h); uint32_t c = ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)(v + 1);
        uint32_t *row = px + (size_t)y * w; for (int x = 0; x < w; x++) row[x] = c; }
    uint32_t s = 12345; for (int i = 0; i < w * h / 180; i++) { s = s * 1103515245 + 12345; int x = (s >> 8) % max(1, w); s = s * 1103515245 + 12345; int y = (s >> 8) % max(1, h);
        uint32_t c = px[(size_t)y * w + x]; int d = (s >> 20) & 1 ? 6 : -6;
        int r = min(255, max(0, (int)((c >> 16) & 255) + d)), g = min(255, max(0, (int)((c >> 8) & 255) + d)), b = min(255, max(0, (int)(c & 255) + d));
        px[(size_t)y * w + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }
    g_bgDC = CreateCompatibleDC(NULL); g_bgOld = SelectObject(g_bgDC, g_bgBmp); g_bgW = w; g_bgH = h;
}
static void paint_bg(HDC dc, RECT rc) {
    int w = rc.right, h = rc.bottom; if (w <= 0 || h <= 0) return;
    if (!g_bgDC || w != g_bgW || h != g_bgH) build_bg(w, h);
    if (g_bgDC) BitBlt(dc, 0, 0, w, h, g_bgDC, 0, 0, SRCCOPY);
    { HGDIOBJ of = SelectObject(dc, g_titleFont); SetBkMode(dc, TRANSPARENT); RECT t = g_titleRect; OffsetRect(&t, 1, 1); SetTextColor(dc, RGB(245, 245, 245));
      DrawTextW(dc, TITLE, -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SetTextColor(dc, RGB(35, 35, 40)); DrawTextW(dc, TITLE, -1, &g_titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of); }
    for (auto &p : g_panels) { if (p.right <= p.left) continue;
        for (int k = S(6); k >= 1; k--) { int v = 150 + 12 * k; HBRUSH b = CreateSolidBrush(RGB(v, v, v)); RECT r = {p.left + k, p.top + k, p.right + k, p.bottom + k}; FillRect(dc, &r, b); DeleteObject(b); }
        FillRect(dc, &p, g_panelBrush); HPEN pen = CreatePen(PS_SOLID, 1, RGB(196, 196, 196)); HGDIOBJ op = SelectObject(dc, pen); HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, p.left, p.top, p.right, p.bottom); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); }
}
