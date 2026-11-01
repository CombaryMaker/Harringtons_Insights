// bigcap.cpp - builds the hs_shark test captures from public pcap collections, no Python or mergecap needed.
//   bigcap DIR base.pcap big8.pcap
// Walks DIR, takes every classic .pcap file whose link type is Ethernet (pcapng and other link types skipped),
// joins their packets into base.pcap (microsecond pcap header), then writes big8.pcap = base repeated 8 times,
// each copy shifted 100,000,000 s later and with the last octet of IPv4 source/destination XORed by the copy number
// (the same construction as make_big8.py). Michael Hugh Harrington, MIT License.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
using namespace std; namespace fs = std::filesystem;
static uint32_t rd32(const uint8_t *p, bool be) { return be ? (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3] : (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "bigcap DIR base.pcap big8.pcap\n"); return 2; }
    vector<string> files; for (auto &e : fs::recursive_directory_iterator(argv[1])) if (e.is_regular_file()) { string x = e.path().extension().string(); transform(x.begin(), x.end(), x.begin(), ::tolower); if (x == ".pcap" || x == ".cap") files.push_back(e.path().string()); }
    sort(files.begin(), files.end());
    vector<uint8_t> base; uint8_t gh[24] = {0xd4, 0xc3, 0xb2, 0xa1, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 1, 0, 0, 0}; base.insert(base.end(), gh, gh + 24);
    size_t used = 0, npk = 0;
    for (auto &f : files) {
        FILE *fi = fopen(f.c_str(), "rb"); if (!fi) continue; vector<uint8_t> d; uint8_t buf[1 << 16]; size_t k; while ((k = fread(buf, 1, sizeof buf, fi)) > 0) d.insert(d.end(), buf, buf + k); fclose(fi);
        if (d.size() < 24) continue; uint32_t magic = rd32(d.data(), false); bool be, nano;
        if (magic == 0xa1b2c3d4) { be = false; nano = false; } else if (magic == 0xd4c3b2a1) { be = true; nano = false; } else if (magic == 0xa1b23c4d) { be = false; nano = true; } else if (magic == 0x4d3cb2a1) { be = true; nano = true; } else continue;
        if ((rd32(d.data() + 20, be) & 0x0fffffff) != 1) continue;   // Ethernet only
        size_t o = 24, n = 0; vector<uint8_t> out;
        while (o + 16 <= d.size()) { uint32_t s = rd32(&d[o], be), us = rd32(&d[o + 4], be), il = rd32(&d[o + 8], be), ol = rd32(&d[o + 12], be); if (il > 262144 || o + 16 + il > d.size()) break;
            if (nano) us /= 1000; uint8_t h[16]; memcpy(h, &s, 4); memcpy(h + 4, &us, 4); memcpy(h + 8, &il, 4); memcpy(h + 12, &ol, 4); out.insert(out.end(), h, h + 16); out.insert(out.end(), &d[o + 16], &d[o + 16 + il]); o += 16 + il; n++; }
        if (!n) continue; base.insert(base.end(), out.begin(), out.end()); used++; npk += n;
    }
    FILE *fb = fopen(argv[2], "wb"); fwrite(base.data(), 1, base.size(), fb); fclose(fb);
    printf("base.pcap: %zu Ethernet captures of %zu files, %zu packets, %zu bytes\n", used, files.size(), npk, base.size());
    FILE *fo = fopen(argv[3], "wb"); fwrite(base.data(), 1, 24, fo); vector<uint8_t> hdr(16), body;
    for (uint32_t c = 0; c < 8; c++) { size_t o = 24;
        while (o + 16 <= base.size()) { memcpy(hdr.data(), &base[o], 16); uint32_t s, il; memcpy(&s, &hdr[0], 4); memcpy(&il, &hdr[8], 4); s += c * 100000000u; memcpy(&hdr[0], &s, 4);
            body.assign(&base[o + 16], &base[o + 16 + il]);
            if (il >= 34) { uint32_t et = body[12] << 8 | body[13]; size_t off = 14; while ((et == 0x8100 || et == 0x88a8) && off + 4 <= il) { et = body[off + 2] << 8 | body[off + 3]; off += 4; }
                if (et == 0x0800 && off + 20 <= il) { body[off + 13] ^= (uint8_t)c; body[off + 17] ^= (uint8_t)c; } }
            fwrite(hdr.data(), 1, 16, fo); fwrite(body.data(), 1, il, fo); o += 16 + il; } }
    fclose(fo); printf("big8.pcap written: %zu packets\n", npk * 8); return 0;
}
