// Decode one .bz2 file many times in-process with the system's stock libbz2.
#include <bzlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *in = malloc(n); if (fread(in, 1, n, f) != (size_t)n) return 1; fclose(f);
    unsigned int cap = (unsigned int)atol(argv[2]) + 64; char *out = malloc(cap);
    int reps = argc > 3 ? atoi(argv[3]) : 50; double *t = malloc(sizeof(double) * reps);
    unsigned int outn = 0;
    for (int r = 0; r < reps; r++) {
        unsigned int on = cap; double t0 = now();
        int rc = BZ2_bzBuffToBuffDecompress(out, &on, in, (unsigned int)n, 0, 0);
        t[r] = now() - t0; if (rc != BZ_OK) { fprintf(stderr, "error %d\n", rc); return 2; } outn = on;
    }
    qsort(t, reps, sizeof(double), cmpd);
    printf("%ld -> %u  best %.1f us  median %.1f us\n", n, outn, t[0] * 1e6, t[reps / 2] * 1e6);
    return 0;
}
