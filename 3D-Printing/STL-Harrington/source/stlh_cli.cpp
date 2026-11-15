// STL-Harrington command-line test tool. Usage: stlh_cli c|cz in.stl out.stlh  |  stlh_cli d in.stlh out.stl
// Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.
#include <cstdio>
#include <chrono>
#include "stlh_core.h"
void libzpaq::error(const char* msg) { fprintf(stderr, "zpaq error: %s\n", msg); exit(3); }
static stlh::Bytes rd(const char* f) { FILE* p = fopen(f, "rb"); stlh::Bytes b; if (!p) return b; fseek(p, 0, 2); b.resize(ftell(p)); fseek(p, 0, 0); if (fread(b.data(), 1, b.size(), p)) {} fclose(p); return b; }
int main(int argc, char** argv) {
    if (argc < 4) return 1; stlh::Bytes in = rd(argv[2]), out; auto t = std::chrono::steady_clock::now(); stlh::Result r;
    std::string m = argv[1], f = argv[2]; bool obj = f.size() > 4 && f.substr(f.size() - 4) == ".obj";
    if (m == "d") r = stlh::restore(in, out); else r = stlh::compress(in, obj, m == "cz" ? stlh::ZSTD : stlh::ZPAQ, out, [](const char*, double) {});
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    if (!r.ok) { printf("FAIL %s\n", r.error.c_str()); return 2; }
    FILE* p = fopen(argv[3], "wb"); fwrite(out.data(), 1, out.size(), p); fclose(p);
    printf("%s %zu -> %zu  tris %zu  order %d  %.2fs\n", m.c_str(), r.inBytes, r.outBytes, r.triangles, r.order, s);
}
