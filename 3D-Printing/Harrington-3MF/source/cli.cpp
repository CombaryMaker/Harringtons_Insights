// command-line test driver for the core (Linux build, for testing only)
// Created with the assistance of Claude AI. The formal directing and structure behind this work are by Michael Hugh Harrington.
#include "s3mf_core.h"
int main(int c, char** v) {
    FILE* f = fopen(v[1], "rb"); fseek(f, 0, 2); std::vector<uint8_t> b(ftell(f)); fseek(f, 0, 0); fread(b.data(), 1, b.size(), f); fclose(f);
    std::string n = v[1]; bool obj = n.size() > 4 && n.substr(n.size() - 4) == ".obj"; std::vector<uint8_t> out;
    auto r = s3mf::encode(b, obj, out, [](const char*, double) {});
    if (!r.ok) { printf("FAILED %s\n", r.error.c_str()); return 1; }
    FILE* o = fopen(v[2], "wb"); fwrite(out.data(), 1, out.size(), o); fclose(o);
    printf("{\"in\":%zu,\"out\":%zu,\"tris\":%zu,\"verts\":%zu,\"dropped\":%zu,\"sorted\":%d,\"verified\":%d}\n", r.inBytes, r.outBytes, r.triangles, r.vertices, r.dropped, r.sorted, r.verified);
}
