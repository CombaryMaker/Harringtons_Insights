// rivals.cpp - small command-line front end for two stock libraries that have no Windows command-line tool of their own.
// Part of the HX Competition Test. (c) 2026 Michael Hugh Harrington, MIT License. The libraries are unmodified:
//   Snappy 1.3.1 (Google, BSD-3) - raw Snappy format (the length is stored by Snappy itself)
//   libdeflate 1.26 (Eric Biggers, MIT) - standard gzip format (RFC 1952), levels 1-12
// usage: rivals snappy c IN OUT | rivals snappy d IN OUT | rivals gzip LEVEL c IN OUT | rivals gzip d IN OUT
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "snappy.h"
#include "libdeflate.h"
static std::vector<char> readf(const char *fn) { FILE *f = fopen(fn, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", fn); exit(1); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); std::vector<char> d(n); if (n && fread(d.data(), 1, n, f) != (size_t)n) exit(1); fclose(f); return d; }
static void writef(const char *fn, const char *d, size_t n) { FILE *f = fopen(fn, "wb"); if (!f) { fprintf(stderr, "cannot write %s\n", fn); exit(1); }
    if (n && fwrite(d, 1, n, f) != n) exit(1); fclose(f); }
int main(int argc, char **argv) {
    if (argc >= 5 && !strcmp(argv[1], "snappy")) {
        std::vector<char> in = readf(argv[3]); std::string out;
        if (!strcmp(argv[2], "c")) { snappy::Compress(in.data(), in.size(), &out); }
        else if (!strcmp(argv[2], "d")) { if (!snappy::Uncompress(in.data(), in.size(), &out)) { fprintf(stderr, "snappy: bad data\n"); return 1; } }
        else return 2;
        writef(argv[4], out.data(), out.size()); return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "gzip") && !strcmp(argv[3], "c")) {
        int lv = atoi(argv[2]); std::vector<char> in = readf(argv[4]);
        libdeflate_compressor *c = libdeflate_alloc_compressor(lv); if (!c) return 1;
        size_t cap = libdeflate_gzip_compress_bound(c, in.size()); std::vector<char> o(cap);
        size_t n = libdeflate_gzip_compress(c, in.data(), in.size(), o.data(), cap); libdeflate_free_compressor(c);
        if (!n) return 1; writef(argv[5], o.data(), n); return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "gzip") && !strcmp(argv[2], "d")) {
        std::vector<char> in = readf(argv[3]); if (in.size() < 18) return 1;
        size_t n = (unsigned char)in[in.size() - 4] | (unsigned char)in[in.size() - 3] << 8 | (unsigned char)in[in.size() - 2] << 16 | (size_t)(unsigned char)in[in.size() - 1] << 24;
        std::vector<char> o(n + 1); size_t got = 0; libdeflate_decompressor *d = libdeflate_alloc_decompressor();
        libdeflate_result r = libdeflate_gzip_decompress(d, in.data(), in.size(), o.data(), n, &got); libdeflate_free_decompressor(d);
        if (r != LIBDEFLATE_SUCCESS) { fprintf(stderr, "gzip: bad data\n"); return 1; } writef(argv[4], o.data(), got); return 0;
    }
    fprintf(stderr, "usage: rivals snappy c|d IN OUT | rivals gzip LEVEL c IN OUT | rivals gzip d IN OUT\n"); return 2;
}
