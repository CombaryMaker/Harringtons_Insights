// bigcapn.cpp - repeat a base capture N times, as bigcap.cpp does for 8 copies: copy c is shifted c*SHIFT seconds later
// and the second octet of IPv4 source/destination is XORed with c. Michael Hugh Harrington, 2026. MIT License.
//   bigcapn BASE.pcap OUT.pcap N SHIFT_SECONDS        (bigcapn base_m.pcap big8m.pcap 8 100000000 = big8m.pcap)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
using namespace std;
int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: bigcapn BASE.pcap OUT.pcap N SHIFT_SECONDS\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    vector<uint8_t> base(n); if (fread(base.data(), 1, n, f) != (size_t)n) return 1; fclose(f);
    uint32_t N = atoi(argv[3]), S = strtoul(argv[4], nullptr, 10); size_t npk = 0;
    FILE *fo = fopen(argv[2], "wb"); fwrite(base.data(), 1, 24, fo); vector<uint8_t> hdr(16), body;
    for (uint32_t c = 0; c < N; c++) { size_t o = 24;
        while (o + 16 <= base.size()) { memcpy(hdr.data(), &base[o], 16); uint32_t s, il; memcpy(&s, &hdr[0], 4); memcpy(&il, &hdr[8], 4); s += c * S; memcpy(&hdr[0], &s, 4);
            body.assign(&base[o + 16], &base[o + 16 + il]);
            if (il >= 34) { uint32_t et = body[12] << 8 | body[13]; size_t off = 14; while ((et == 0x8100 || et == 0x88a8) && off + 4 <= il) { et = body[off + 2] << 8 | body[off + 3]; off += 4; }
                if (et == 0x0800 && off + 20 <= il) { body[off + 13] ^= (uint8_t)c; body[off + 17] ^= (uint8_t)c; } }
            fwrite(hdr.data(), 1, 16, fo); fwrite(body.data(), 1, il, fo); o += 16 + il; npk++; } }
    fclose(fo); fprintf(stderr, "%s written: %u copies, %zu packets\n", argv[2], N, npk); return 0;
}
