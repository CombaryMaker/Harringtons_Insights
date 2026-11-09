// hx_win_compat.h - lets HX build as a native Windows program (MinGW / clang). Michael Hugh Harrington, Nov 2026.
// Patent Pending (U.S. Provisional App. 64/192,843). Same license as HX (see LICENSE.txt).
// Supplies the few POSIX calls HX uses: setenv, unsetenv, mkdtemp, and a system() that keeps quoted paths intact.
#pragma once
#ifdef _WIN32
#define HX_NULL "NUL"
#define HX_CD "cd /d "
#define HX_NOEMBED_PREFIX "set HX_NOEMBED=1&& "
#else
#define HX_NULL "/dev/null"
#define HX_CD "cd "
#define HX_NOEMBED_PREFIX "HX_NOEMBED=1 "
#endif
#ifdef _WIN32
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <direct.h>
#include <io.h>
#include <process.h>
static inline int setenv(const char *k, const char *v, int) { return _putenv_s(k, v); }
static inline int unsetenv(const char *k) { return _putenv_s(k, ""); }
static inline char *mkdtemp(char *tmpl) {          // fills the XXXXXX in place and creates the folder
    size_t L = strlen(tmpl); if (L < 6) return nullptr;
    for (int tries = 0; tries < 200; tries++) {
        for (int i = 0; i < 6; i++) tmpl[L - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[(rand() + _getpid() * 7 + tries * 13 + i * 31) % 36];
        if (_mkdir(tmpl) == 0) return tmpl;
    }
    return nullptr;
}
static inline const char *hx_tmpdir() { const char *w = getenv("TEMP"); if (!w) w = getenv("TMP"); return w ? w : "."; }
// cmd.exe strips the first and last quote of a command line that starts with a quote; one extra pair keeps paths intact.
static inline int hx_system(const char *cmd) { std::string c = std::string("\"") + cmd + "\""; return system(c.c_str()); }
#else
static inline const char *hx_tmpdir() { return "/tmp"; }
static inline int hx_system(const char *cmd) { return system(cmd); }
#endif
