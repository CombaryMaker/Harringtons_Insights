// hs_pcapsplit.cpp - Harrington Sets for one capture (classic pcap, Ethernet). Michael Hugh Harrington, 2026. MIT License.
// Every packet joins a conversation set by host pair (both IP addresses, either direction). DHCP (UDP 67/68) is one set,
// since one DHCP exchange spans several address pairs; other non-IP traffic is one set. Sets are spread over N shards
// by bytes, largest first (Graham LPT); each shard keeps original packet order.
//   hs_pcapsplit IN.pcap N OUTDIR
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <queue>
using namespace std;
int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: hs_pcapsplit IN.pcap N OUTDIR\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1; int k = atoi(argv[2]);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); vector<uint8_t> d(n); if (fread(d.data(), 1, n, f) != (size_t)n) return 1; fclose(f);
    unordered_map<string, int> id; vector<uint64_t> sz; vector<pair<size_t, int>> pk; size_t p = 24;
    while (p + 16 <= (size_t)n) {
        uint32_t cl; memcpy(&cl, &d[p + 8], 4); const uint8_t *b = &d[p + 16]; size_t L = min<size_t>(cl, n - p - 16), o = 14; string key;
        uint16_t et = L >= 14 ? (b[12] << 8 | b[13]) : 0;
        while ((et == 0x8100 || et == 0x88a8) && L >= o + 4) { et = b[o + 2] << 8 | b[o + 3]; o += 4; }
        int proto = -1; size_t l4 = 0; string a, c;
        if (et == 0x0800 && L >= o + 20) { a.assign((const char*)b + o + 12, 4); c.assign((const char*)b + o + 16, 4); proto = b[o + 9]; l4 = o + (b[o] & 15) * 4; }
        else if (et == 0x86dd && L >= o + 40) { a.assign((const char*)b + o + 8, 16); c.assign((const char*)b + o + 24, 16); proto = b[o + 6]; l4 = o + 40; }
        if (proto == 17 && L >= l4 + 4) { int sp = b[l4] << 8 | b[l4 + 1], dp = b[l4 + 2] << 8 | b[l4 + 3]; if (sp == 67 || sp == 68 || dp == 67 || dp == 68) key = "DHCP"; }
        if (key.empty()) key = proto < 0 ? string("NONIP") : min(a, c) + max(a, c);
        auto it = id.find(key); int s; if (it == id.end()) { s = sz.size(); id[key] = s; sz.push_back(0); } else s = it->second;
        sz[s] += 16 + cl; pk.push_back({p, s}); p += 16 + cl;
    }
    vector<int> ord(sz.size()); for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    sort(ord.begin(), ord.end(), [&](int x, int y) { return sz[x] > sz[y]; });
    priority_queue<pair<uint64_t, int>, vector<pair<uint64_t, int>>, greater<>> h; for (int i = 0; i < k; i++) h.push({0, i});
    vector<int> shard(sz.size()); for (int s : ord) { auto t = h.top(); h.pop(); shard[s] = t.second; h.push({t.first + sz[s], t.second}); }
    vector<FILE*> fo(k); for (int i = 0; i < k; i++) { char nm[4096]; snprintf(nm, sizeof nm, "%s/shard%02d.pcap", argv[3], i); fo[i] = fopen(nm, "wb"); fwrite(d.data(), 1, 24, fo[i]); }
    for (auto &q : pk) { uint32_t cl; memcpy(&cl, &d[q.first + 8], 4); fwrite(&d[q.first], 1, 16 + cl, fo[shard[q.second]]); }
    for (auto x : fo) fclose(x);
    printf("%zu packets, %zu sets, %d shards\n", pk.size(), sz.size(), k); return 0;
}
