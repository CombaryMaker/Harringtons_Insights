// zpaq stream front end for the head-to-head app. Michael Hugh Harrington, MIT License, 2026.
// Uses libzpaq 7.15 (Matt Mahoney, public domain): built once with the stock libzpaq.cpp ("zpaq-stock") and once
// with zpaq-hi's libzpaq.cpp ("zpaq-hi"). Only compressBlock() differs; both decode with the stock decoder.
//   zpaq-xxx c METHOD IN OUT     (METHOD e.g. 5 = the archiver's -m5)
//   zpaq-xxx d IN OUT
#include "libzpaq.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
void libzpaq::error(const char *msg) { fprintf(stderr, "zpaq error: %s\n", msg); exit(1); }
struct FR : libzpaq::Reader { FILE *f; int get() { return getc(f); } int read(char *b, int n) { return (int)fread(b, 1, n, f); } };
struct FW : libzpaq::Writer { FILE *f; void put(int c) { putc(c, f); } void write(const char *b, int n) { fwrite(b, 1, n, f); } };
int main(int argc, char **argv) {
    bool c = argc == 5 && !strcmp(argv[1], "c"), d = argc == 4 && !strcmp(argv[1], "d");
    if (!c && !d) { fprintf(stderr, "usage: %s c METHOD IN OUT | %s d IN OUT\n", argv[0], argv[0]); return 2; }
    FR in; FW out; in.f = fopen(argv[c ? 3 : 2], "rb"); if (!in.f) { perror(argv[c ? 3 : 2]); return 1; }
    out.f = fopen(argv[c ? 4 : 3], "wb"); if (!out.f) { perror(argv[c ? 4 : 3]); return 1; }
    if (c) libzpaq::compress(&in, &out, argv[2]); else libzpaq::decompress(&in, &out);
    fclose(in.f); if (fclose(out.f)) { perror("write"); return 1; }
    return 0;
}
