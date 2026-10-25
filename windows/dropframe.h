// Shared drag-and-drop front end for bzip2-hi and xz-hi (Harrington Insights).
// Drop files on the program: a normal file is compressed to NAME.EXT after a
// check that it decodes back exactly; a NAME.EXT file is restored.
// Command line: PROGRAM c IN OUT | PROGRAM d IN OUT
// MIT License, Copyright (c) 2026 Michael Harrington.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
typedef wchar_t pchar;
#define PS "%ls"
static FILE *open_file(const pchar *p, int wr) { return _wfopen(p, wr ? L"wb" : L"rb"); }
static size_t plen(const pchar *s) { return wcslen(s); }
#else
typedef char pchar;
#define PS "%s"
static FILE *open_file(const pchar *p, int wr) { return fopen(p, wr ? "wb" : "rb"); }
static size_t plen(const pchar *s) { return strlen(s); }
#endif
static pchar plower(pchar c) { return (c >= 'A' && c <= 'Z') ? (pchar)(c + 32) : c; }
enum { PATHMAX = 4096 };

static int file_exists(const pchar *p) { FILE *f = open_file(p, 0); if (f) { fclose(f); return 1; } return 0; }
static int read_file(const pchar *p, uint8_t **data, size_t *n) {
    FILE *f = open_file(p, 0);
    if (!f) return -1;
    size_t cap = 1 << 20, len = 0;
    uint8_t *d = (uint8_t *)malloc(cap);
    for (;;) {
        if (!d) { fclose(f); return -4; }
        size_t got = fread(d + len, 1, cap - len, f);
        len += got;
        if (len < cap) break;
        if (cap >= ((size_t)1 << 31)) { free(d); fclose(f); return -3; }
        uint8_t *nd = (uint8_t *)realloc(d, cap * 2);
        if (!nd) { free(d); fclose(f); return -4; }
        d = nd; cap *= 2;
    }
    fclose(f);
    *data = d; *n = len;
    return 0;
}
static int write_file(const pchar *p, const uint8_t *data, size_t n) {
    FILE *f = open_file(p, 1);
    if (!f) return -1;
    size_t put = fwrite(data, 1, n, f);
    int bad = fclose(f) != 0 || put != n;
    return bad ? -1 : 0;
}
static void pcopy(pchar *dst, const pchar *src, size_t n) { memcpy(dst, src, n * sizeof(pchar)); dst[n] = 0; }
static void pappend_ascii(pchar *dst, const char *s) { size_t n = plen(dst); while (*s && n < PATHMAX - 1) dst[n++] = (pchar)*s++; dst[n] = 0; }
static int ends_with_ext(const pchar *p, const char *ext) {   // ext like ".xz"
    size_t n = plen(p), e = strlen(ext);
    if (n <= e) return 0;
    for (size_t i = 0; i < e; i++) if (plower(p[n - e + i]) != ext[i]) return 0;
    return 1;
}
static const pchar *base_name(const pchar *p) { const pchar *b = p; for (const pchar *s = p; *s; s++) if (*s == '/' || *s == '\\') b = s + 1; return b; }
// NAME.EXT, or NAME (2).EXT ... if taken.
static int name_for_compressed(const pchar *in, const char *ext, pchar *out) {
    size_t n = plen(in);
    if (n + 16 >= PATHMAX) return -1;
    for (int k = 1; k < 1000; k++) {
        pcopy(out, in, n);
        if (k > 1) { char tag[16]; snprintf(tag, sizeof tag, " (%d)", k); pappend_ascii(out, tag); }
        pappend_ascii(out, ext);
        if (!file_exists(out)) return 0;
    }
    return -1;
}
// NAME.EXT -> NAME, or NAME (restored) if taken.
static int name_for_restored(const pchar *in, const char *ext, pchar *out) {
    size_t n = plen(in) - strlen(ext);
    if (n + 32 >= PATHMAX) return -1;
    pcopy(out, in, n);
    if (!file_exists(out)) return 0;
    for (int k = 1; k < 1000; k++) {
        pcopy(out, in, n);
        char tag[32];
        if (k == 1) snprintf(tag, sizeof tag, " (restored)"); else snprintf(tag, sizeof tag, " (restored %d)", k);
        pappend_ascii(out, tag);
        if (!file_exists(out)) return 0;
    }
    return -1;
}
static const char *commas(uint64_t v, char *buf) {
    char tmp[32]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    int k = 0;
    for (int i = n - 1; i >= 0; i--) { buf[k++] = tmp[i]; if (i > 0 && i % 3 == 0) buf[k++] = ','; }
    buf[k] = 0;
    return buf;
}
static void report_read_error(int code) {
    if (code == -1) printf("  Could not open or read the file.\n");
    else if (code == -3) printf("  The file is too large (the limit is 2 GB).\n");
    else if (code == -4) printf("  Not enough memory.\n");
}

// ---- each tool defines TOOL_NAME, TOOL_EXT, TOOL_BASELINE, tool_compress and
// tool_decompress before including this file.

static int do_compress(const pchar *in, const pchar *out_given) {
    char a[32], b[32], c[32], note[128] = "";
    uint8_t *data = NULL, *blob = NULL, *back = NULL;
    size_t n = 0, bn = 0, backn = 0, basen = 0;
    printf("Compressing " PS "\n", base_name(in));
    int rc = read_file(in, &data, &n);
    if (rc) { report_read_error(rc); return 1; }
    clock_t t0 = clock();
    if (tool_compress(data, n, &blob, &bn, &basen, note, sizeof note)) { printf("  Compression failed.\n"); free(data); return 1; }
    double tc = (double)(clock() - t0) / CLOCKS_PER_SEC;
    t0 = clock();
    rc = tool_decompress(blob, bn, &back, &backn);
    double td = (double)(clock() - t0) / CLOCKS_PER_SEC;
    int same = rc == 0 && backn == n && (n == 0 || memcmp(back, data, n) == 0);
    free(back); free(data);
    if (!same) { printf("  ERROR: the check decode did not match. Nothing was saved.\n"); free(blob); return 1; }
    static pchar outp[PATHMAX];
    if (out_given) pcopy(outp, out_given, plen(out_given));
    else if (name_for_compressed(in, TOOL_EXT, outp)) { printf("  Could not choose an output name.\n"); free(blob); return 1; }
    if (write_file(outp, blob, bn)) { printf("  Could not write " PS "\n", outp); free(blob); return 1; }
    free(blob);
    printf("  %s bytes -> %s bytes   (1,000,000 bits down to %s bits)\n", commas(n, a), commas(bn, b), commas(n ? (uint64_t)((double)bn * 1e6 / n + 0.5) : 0, c));
    if (basen) printf("  %s would be %s bytes\n", TOOL_BASELINE, commas(basen, a));
    if (note[0]) printf("  %s\n", note);
    printf("  compress %.2f s, check decode %.3f s, decoded back exactly\n", tc, td);
    printf("  Saved " PS "\n\n", base_name(outp));
    return 0;
}
static int do_decompress(const pchar *in, const pchar *out_given) {
    char a[32], b[32];
    uint8_t *blob = NULL, *data = NULL;
    size_t bn = 0, n = 0;
    printf("Restoring " PS "\n", base_name(in));
    int rc = read_file(in, &blob, &bn);
    if (rc) { report_read_error(rc); return 1; }
    clock_t t0 = clock();
    rc = tool_decompress(blob, bn, &data, &n);
    double td = (double)(clock() - t0) / CLOCKS_PER_SEC;
    free(blob);
    if (rc) { printf("  This is not a valid %s file, or it is damaged. Nothing was saved.\n", TOOL_EXT); return 1; }
    static pchar outp[PATHMAX];
    if (out_given) pcopy(outp, out_given, plen(out_given));
    else if (name_for_restored(in, TOOL_EXT, outp)) { printf("  Could not choose an output name.\n"); free(data); return 1; }
    if (write_file(outp, data, n)) { printf("  Could not write " PS "\n", outp); free(data); return 1; }
    free(data);
    printf("  %s bytes -> %s bytes in %.3f s\n  Saved " PS "\n\n", commas(bn, a), commas(n, b), td, base_name(outp));
    return 0;
}
static int is_word(const pchar *s, char c) { return s[0] == (pchar)c && s[1] == 0; }
static int run(int argc, pchar **argv) {
    if (argc == 4 && (is_word(argv[1], 'c') || is_word(argv[1], 'd')))
        return is_word(argv[1], 'd') ? do_decompress(argv[2], argv[3]) : do_compress(argv[2], argv[3]);
    printf("%s  (Harrington Insights)\nMIT License, Copyright (c) 2026 Michael Harrington.\n\n", TOOL_NAME);
    int failures = 0;
    if (argc < 2) {
        printf("To compress a file, drag it onto this program's icon.\n");
        printf("It is saved next to the original as NAME%s, after a check\nthat it decodes back exactly.\n\n", TOOL_EXT);
        printf("To restore a file, drag the %s file onto the icon.\n\n", TOOL_EXT);
    } else {
        for (int i = 1; i < argc; i++)
            failures += ends_with_ext(argv[i], TOOL_EXT) ? do_decompress(argv[i], NULL) : do_compress(argv[i], NULL);
    }
#ifdef _WIN32
    printf("Press Enter to close this window.");
    fflush(stdout);
    getchar();
#endif
    return failures ? 1 : 0;
}
#ifdef _WIN32
int main(void) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    int rc = run(argc, argv);
    LocalFree(argv);
    return rc;
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
