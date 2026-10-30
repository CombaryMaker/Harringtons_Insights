/* test driver: compress a file with the tuned libbz2 encoder at block size -L, write to stdout */
#include <stdio.h>
#include <stdlib.h>
#include "bzlib.h"
int main(int c, char **v) {
  int L = atoi(v[1]); FILE *f = fopen(v[2], "rb"); fseek(f, 0, 2); long n = ftell(f); fseek(f, 0, 0);
  char *in = malloc(n + 1); if (n) fread(in, 1, n, f); fclose(f);
  unsigned int cap = n + n / 100 + 600 + 64; char *out = malloc(cap);
  int r = BZ2_bzBuffToBuffCompress(out, &cap, in, n, L, 0, 0);
  if (r != BZ_OK) { fprintf(stderr, "error %d\n", r); return 1; }
  fwrite(out, 1, cap, stdout); return 0;
}
