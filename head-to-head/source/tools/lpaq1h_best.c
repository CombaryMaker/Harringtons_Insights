/* lpaq1-h best-of driver (native port of lpaq1h_best.py). Michael Hugh Harrington, MIT License, 2026.
 * Runs lpaq1-h.exe (same folder, GPL) once per flag setting and keeps the smallest output.
 *   lpaq1-h-best N IN OUT        (decode with: lpaq1-h d IN OUT) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define SETENV(k, v) _putenv_s(k, v)
#else
#include <unistd.h>
#include <sys/wait.h>
#define SETENV(k, v) setenv(k, v, 1)
#endif
static long fsize(const char *f) { FILE *p = fopen(f, "rb"); if (!p) return -1; fseek(p, 0, SEEK_END); long s = ftell(p); fclose(p); return s; }
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: lpaq1-h-best N IN OUT\n"); return 2; }
    char exe[4096]; strncpy(exe, argv[0], sizeof exe - 32); exe[sizeof exe - 32] = 0;
    char *sl = strrchr(exe, '\\'), *s2 = strrchr(exe, '/'); if (s2 > sl) sl = s2;
    if (sl) strcpy(sl + 1, "lpaq1-h.exe"); else strcpy(exe, "lpaq1-h.exe");
    static const int flags[6] = {0, 4, 9, 13, 11, 15};
    char tmp[4200]; snprintf(tmp, sizeof tmp, "%s.tmp", argv[3]); long best = -1; int bf = 0;
    for (int i = 0; i < 6; i++) {
        char v[8]; snprintf(v, sizeof v, "%d", flags[i]); SETENV("LPH_FLAGS", v);
#ifdef _WIN32
        char q1[4200], q2[4200], q3[4200]; snprintf(q1, sizeof q1, "\"%s\"", argv[2]); snprintf(q2, sizeof q2, "\"%s\"", tmp); snprintf(q3, sizeof q3, "\"%s\"", exe);
        const char *a[] = {q3, argv[1], q1, q2, NULL};
        intptr_t rc = _spawnv(_P_WAIT, exe, a);
#else
        pid_t p = fork(); if (!p) { execl(exe, exe, argv[1], argv[2], tmp, (char *)NULL); _exit(127); } int st; waitpid(p, &st, 0); int rc = WEXITSTATUS(st);
#endif
        if (rc != 0) { fprintf(stderr, "lpaq1-h failed (flags %d)\n", flags[i]); return 1; }
        long s = fsize(tmp);
        if (best < 0 || s < best) { best = s; bf = flags[i]; remove(argv[3]); if (rename(tmp, argv[3])) { perror("rename"); return 1; } }
        else remove(tmp);
    }
    printf("%s -> %s  %ld bytes (flags %d)\n", argv[2], argv[3], best, bf);
    return 0;
}
