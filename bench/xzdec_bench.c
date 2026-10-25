// Decode one .xz file many times in-process with liblzma; print best and median time per decode.
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *in = malloc(n); if (fread(in, 1, n, f) != (size_t)n) return 1; fclose(f);
    size_t cap = (size_t)atol(argv[2]) + 64; uint8_t *out = malloc(cap);
    int reps = argc > 3 ? atoi(argv[3]) : 100; double *t = malloc(sizeof(double) * reps);
    size_t outn = 0;
    for (int r = 0; r < reps; r++) {
        uint64_t memlimit = UINT64_MAX; size_t ip = 0, op = 0;
        double t0 = now();
        lzma_ret rc = lzma_stream_buffer_decode(&memlimit, 0, NULL, in, &ip, n, out, &op, cap);
        t[r] = now() - t0;
        if (rc != LZMA_OK) { fprintf(stderr, "decode error %d\n", rc); return 2; }
        outn = op;
    }
    qsort(t, reps, sizeof(double), cmpd);
    printf("%ld bytes -> %zu  best %.1f us  median %.1f us\n", n, outn, t[0] * 1e6, t[reps / 2] * 1e6);
    return 0;
}
