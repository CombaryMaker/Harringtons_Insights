// hs_pcapsplit.cpp - Harrington Sets for one capture (classic pcap, Ethernet). Michael Hugh Harrington, 2026. MIT License.
// Every packet joins a conversation set by host pair (both IP addresses, either direction). DHCP (UDP 67/68) is one set,
// since one DHCP exchange spans several address pairs; other non-IP traffic is one set. Sets are spread over N shards
// by bytes, largest first (Graham LPT); each shard keeps original packet order.
// --ticks (for Zeek): every shard also gets a 60-byte clock-tick frame (EtherType 0x88B5, IEEE local experimental,
// which Zeek reads without logging anything) at the time of each packet that went to another shard, skipped when the
// shard's clock is already at that time. Each shard's Zeek clock then advances exactly as in the plain run, so timers
// (connection timeouts, table expiry, delayed events) fire at the same network times.
// --ticks=G (G in microseconds): thinned ticks - a tick is written only when the shard's clock is at least G behind.
//   hs_pcapsplit IN.pcap N OUTDIR [--ticks | --ticks=G]
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
    bool ticks = argc > 4 && string(argv[4]).rfind("--ticks", 0) == 0; uint64_t gap = 0;
    if (ticks && string(argv[4]).size() > 8) gap = strtoull(argv[4] + 8, nullptr, 10);
    vector<uint64_t> last(k, ~0ull), lastus(k, 0); size_t nt = 0;
    uint8_t tick[16 + 60] = {0}; uint32_t tl = 60; memcpy(tick + 8, &tl, 4); memcpy(tick + 12, &tl, 4);
    const uint8_t eth[14] = {2, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 2, 0x88, 0xb5}; memcpy(tick + 16, eth, 14);
    for (auto &q : pk) {
        uint32_t cl; memcpy(&cl, &d[q.first + 8], 4); uint64_t t; memcpy(&t, &d[q.first], 8); int dst = shard[q.second];
        uint32_t ts_s, ts_u; memcpy(&ts_s, &d[q.first], 4); memcpy(&ts_u, &d[q.first + 4], 4); uint64_t us = (uint64_t)ts_s * 1000000 + ts_u;
        fwrite(&d[q.first], 1, 16 + cl, fo[dst]); last[dst] = t; lastus[dst] = us;
        if (ticks) for (int j = 0; j < k; j++) if (j != dst && last[j] != t && us - lastus[j] >= gap) { memcpy(tick, &d[q.first], 8); fwrite(tick, 1, sizeof tick, fo[j]); last[j] = t; lastus[j] = us; nt++; }
    }
    for (auto x : fo) fclose(x);
    printf("%zu packets, %zu sets, %d shards, %zu clock ticks\n", pk.size(), sz.size(), k, nt); return 0;
}
