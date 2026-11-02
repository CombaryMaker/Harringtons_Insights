// hs_zeek_par.cpp - Harrington's Sets runner for Zeek, streaming mode (no shard files).
// Michael Hugh Harrington, 2026. MIT License.
//
// Same sets, shards and clock ticks as hs_pcapsplit --ticks=G, but nothing is written to disk: the capture is mapped
// into memory, a plan is made (every packet to a conversation set by host pair, DHCP one set, non-IP one set; sets to
// shards by bytes, largest first; a 60-byte EtherType 0x88B5 clock tick to a shard whenever its clock is G or more
// microseconds behind another shard's packet), then N Zeek processes are started, each reading its shard from a pipe
// (zeek -r -), and N writer threads feed them at once. Each Zeek starts working on its first packet instead of waiting
// for a split to finish. Prints the time of the plan, and when each Zeek finished.
//
//   hs_zeek_par IN.pcap N OUTDIR GAP_US -- ZEEK [ZEEK_ARGS...]
//   (each Zeek runs in OUTDIR/sNN as: ZEEK -C -r - ZEEK_ARGS...)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
using namespace std;
static double now() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char **argv) {
    int dd = 0; for (int i = 1; i < argc; i++) if (string(argv[i]) == "--") { dd = i; break; }
    if (dd != 5 || dd + 1 >= argc) { fprintf(stderr, "usage: hs_zeek_par IN.pcap N OUTDIR GAP_US -- ZEEK [ARGS...]\n"); return 1; }
    double t0 = now(); signal(SIGPIPE, SIG_IGN);
    int fd = open(argv[1], O_RDONLY); if (fd < 0) { perror("open"); return 1; }
    struct stat st; fstat(fd, &st); size_t n = st.st_size;
    const uint8_t *d = (const uint8_t *)mmap(nullptr, n, PROT_READ, MAP_PRIVATE | (n < (2ull << 30) ? MAP_POPULATE : 0), fd, 0);   // preload only small files
    if (d == MAP_FAILED) { perror("mmap"); return 1; }
    madvise((void *)d, n, MADV_SEQUENTIAL);
    int k = atoi(argv[2]); string od = argv[3]; uint64_t gap = strtoull(argv[4], nullptr, 10);

    // plan 1: sets
    unordered_map<string, int> id; vector<uint64_t> sz; vector<pair<size_t, int>> pk; size_t p = 24;
    while (p + 16 <= n) {
        uint32_t cl; memcpy(&cl, d + p + 8, 4); const uint8_t *b = d + p + 16; size_t L = min<size_t>(cl, n - p - 16), o = 14; string key;
        uint16_t et = L >= 14 ? (b[12] << 8 | b[13]) : 0;
        while ((et == 0x8100 || et == 0x88a8) && L >= o + 4) { et = b[o + 2] << 8 | b[o + 3]; o += 4; }
        int proto = -1; size_t l4 = 0; string a, c;
        if (et == 0x0800 && L >= o + 20) { a.assign((const char *)b + o + 12, 4); c.assign((const char *)b + o + 16, 4); proto = b[o + 9]; l4 = o + (b[o] & 15) * 4; }
        else if (et == 0x86dd && L >= o + 40) { a.assign((const char *)b + o + 8, 16); c.assign((const char *)b + o + 24, 16); proto = b[o + 6]; l4 = o + 40; }
        if (proto == 17 && L >= l4 + 4) { int sp = b[l4] << 8 | b[l4 + 1], dp = b[l4 + 2] << 8 | b[l4 + 3]; if (sp == 67 || sp == 68 || dp == 67 || dp == 68) key = "DHCP"; }
        if (key.empty()) key = proto < 0 ? string("NONIP") : min(a, c) + max(a, c);
        auto it = id.find(key); int s; if (it == id.end()) { s = sz.size(); id[key] = s; sz.push_back(0); } else s = it->second;
        sz[s] += 16 + cl; pk.push_back({p, s}); p += 16 + cl;
    }
    // plan 2: shards (largest set first to the least loaded shard)
    vector<int> ord(sz.size()); for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    sort(ord.begin(), ord.end(), [&](int x, int y) { return sz[x] > sz[y]; });
    priority_queue<pair<uint64_t, int>, vector<pair<uint64_t, int>>, greater<>> h; for (int i = 0; i < k; i++) h.push({0, i});
    vector<int> shard(sz.size()); for (int s : ord) { auto t = h.top(); h.pop(); shard[s] = t.second; h.push({t.first + sz[s], t.second}); }
    // plan 3: per-shard record lists; top bit set = clock tick carrying that packet's timestamp
    const uint64_t TICK = 1ull << 63; vector<vector<uint64_t>> rec(k); vector<uint64_t> last(k, ~0ull), lastus(k, 0), bytes(k, 0), npk(k, 0), ntk(k, 0);
    for (auto &q : pk) {
        uint64_t t; memcpy(&t, d + q.first, 8); uint32_t s_, u_; memcpy(&s_, d + q.first, 4); memcpy(&u_, d + q.first + 4, 4);
        uint64_t us = (uint64_t)s_ * 1000000 + u_; int dst = shard[q.second]; uint32_t cl; memcpy(&cl, d + q.first + 8, 4);
        rec[dst].push_back(q.first); last[dst] = t; lastus[dst] = us; bytes[dst] += 16 + cl; npk[dst]++;
        for (int j = 0; j < k; j++) if (j != dst && last[j] != t && us - lastus[j] >= gap) { rec[j].push_back(q.first | TICK); last[j] = t; lastus[j] = us; ntk[j]++; }
    }
    double t1 = now();
    // start Zeek processes (before any thread exists)
    vector<int> wfd(k); vector<pid_t> pid(k);
    for (int i = 0; i < k; i++) {
        char dir[4096]; snprintf(dir, sizeof dir, "%s/s%02d", od.c_str(), i);
        for (char *q = dir + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(dir, 0755); *q = '/'; }   // create OUTDIR and its parents
        mkdir(dir, 0755);
        int pp[2]; if (pipe2(pp, O_CLOEXEC) != 0) { perror("pipe"); return 1; }
        fcntl(pp[1], F_SETPIPE_SZ, 1 << 20);
        pid_t c = fork();
        if (c == 0) {
            if (chdir(dir) != 0) _exit(127);
            dup2(pp[0], 0); int nul = open("/dev/null", O_WRONLY); dup2(nul, 1); int er = open("zeek_stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644); dup2(er >= 0 ? er : nul, 2);
            vector<char *> av; av.push_back(argv[dd + 1]); av.push_back((char *)"-C"); av.push_back((char *)"-r"); av.push_back((char *)"-");
            for (int a = dd + 2; a < argc; a++) av.push_back(argv[a]); av.push_back(nullptr);
            execv(argv[dd + 1], av.data()); _exit(127);
        }
        close(pp[0]); wfd[i] = pp[1]; pid[i] = c;
    }
    // feed
    vector<thread> th;
    for (int i = 0; i < k; i++) th.emplace_back([&, i]() {
        vector<uint8_t> buf; buf.reserve(1 << 22); buf.insert(buf.end(), d, d + 24);
        uint8_t tick[16 + 60] = {0}; uint32_t tl = 60; memcpy(tick + 8, &tl, 4); memcpy(tick + 12, &tl, 4);
        const uint8_t eth[14] = {2, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 2, 0x88, 0xb5}; memcpy(tick + 16, eth, 14);
        auto flush = [&]() { size_t o = 0; while (o < buf.size()) { ssize_t w = write(wfd[i], buf.data() + o, buf.size() - o); if (w <= 0) { buf.clear(); return false; } o += w; } buf.clear(); return true; };
        for (uint64_t r : rec[i]) {
            if (r & TICK) { memcpy(tick, d + (r & ~TICK), 8); buf.insert(buf.end(), tick, tick + sizeof tick); }
            else { uint32_t cl; memcpy(&cl, d + r + 8, 4); buf.insert(buf.end(), d + r, d + r + 16 + cl); }
            if (buf.size() >= (1 << 22) && !flush()) break;
        }
        flush(); close(wfd[i]);
    });
    vector<double> fin(k, 0); int left = k, bad = 0;
    while (left > 0) { int stt; pid_t w = wait(&stt); if (w < 0) break; for (int i = 0; i < k; i++) if (pid[i] == w) { fin[i] = now() - t0; left--; if (!WIFEXITED(stt) || WEXITSTATUS(stt) != 0) bad++; } }
    for (auto &t : th) t.join();
    double t2 = now();
    printf("plan %.2f s, zeek %.2f s (%d processes, %d failed), %zu packets, %zu sets\n", t1 - t0, t2 - t1, k, bad, pk.size(), sz.size());
    for (int i = 0; i < k; i++) printf("  shard %02d: %8.1f MB, %8lu packets, %8lu ticks, done at %6.2f s\n", i, bytes[i] / 1e6, (unsigned long)npk[i], (unsigned long)ntk[i], fin[i]);
    return bad ? 2 : 0;
}
