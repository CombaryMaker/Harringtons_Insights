// hs_zeek_par1.cpp - Harrington's Sets runner for Zeek, one-pass streaming mode. Michael Hugh Harrington, 2026. MIT License.
//
// For captures larger than memory: the capture is read once, front to back. Each packet joins its conversation set
// (host pair, either direction; DHCP one set; non-IP one set). A set is given a shard the first time it is seen: the
// shard with the fewest bytes so far. Clock ticks (60-byte EtherType 0x88B5 frames) go to a shard whenever its clock is
// GAP or more microseconds behind another shard's packet. Packets and ticks are copied into per-shard queues, and one
// writer thread per shard feeds its Zeek through a pipe (zeek -r -). Nothing is read twice and nothing goes to disk.
// (hs_zeek_par plans first and balances better, but reads the capture twice.)
//
//   hs_zeek_par1 IN.pcap N OUTDIR GAP_US -- ZEEK [ZEEK_ARGS...]     (each Zeek runs in OUTDIR/sNN: ZEEK -C -r - ARGS)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
using namespace std;
static double now() { return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count(); }
struct Q {   // bounded queue of 4 MB chunks for one shard
    mutex m; condition_variable cv; deque<vector<uint8_t>> q; bool done = false; size_t maxq = 16;
    void push(vector<uint8_t> &&c) { unique_lock<mutex> l(m); cv.wait(l, [&] { return q.size() < maxq; }); q.push_back(move(c)); cv.notify_all(); }
    bool pop(vector<uint8_t> &c) { unique_lock<mutex> l(m); cv.wait(l, [&] { return !q.empty() || done; }); if (q.empty()) return false; c = move(q.front()); q.pop_front(); cv.notify_all(); return true; }
    void finish() { unique_lock<mutex> l(m); done = true; cv.notify_all(); }
};
int main(int argc, char **argv) {
    int dd = 0; for (int i = 1; i < argc; i++) if (string(argv[i]) == "--") { dd = i; break; }
    if (dd != 5 || dd + 1 >= argc) { fprintf(stderr, "usage: hs_zeek_par1 IN.pcap N OUTDIR GAP_US -- ZEEK [ARGS...]\n"); return 1; }
    double t0 = now(); signal(SIGPIPE, SIG_IGN);
    int fd = open(argv[1], O_RDONLY); if (fd < 0) { perror("open"); return 1; }
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    int k = atoi(argv[2]); string od = argv[3]; uint64_t gap = strtoull(argv[4], nullptr, 10);
    uint8_t gh[24]; if (read(fd, gh, 24) != 24) return 1;
    // start Zeek processes before any thread exists
    vector<int> wfd(k); vector<pid_t> pid(k);
    for (int i = 0; i < k; i++) {
        char dir[4096]; snprintf(dir, sizeof dir, "%s/s%02d", od.c_str(), i);
        for (char *q = dir + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(dir, 0755); *q = '/'; }
        mkdir(dir, 0755);
        int pp[2]; if (pipe2(pp, O_CLOEXEC) != 0) { perror("pipe"); return 1; }
        fcntl(pp[1], F_SETPIPE_SZ, 1 << 20);
        pid_t c = fork();
        if (c == 0) {
            if (chdir(dir) != 0) _exit(127);
            dup2(pp[0], 0); int nul = open("/dev/null", O_WRONLY); dup2(nul, 1);
            int er = open("zeek_stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644); dup2(er >= 0 ? er : nul, 2);
            vector<char *> av; av.push_back(argv[dd + 1]); av.push_back((char *)"-C"); av.push_back((char *)"-r"); av.push_back((char *)"-");
            for (int a = dd + 2; a < argc; a++) av.push_back(argv[a]); av.push_back(nullptr);
            execv(argv[dd + 1], av.data()); _exit(127);
        }
        close(pp[0]); wfd[i] = pp[1]; pid[i] = c;
    }
    vector<Q> qs(k); vector<thread> th;
    for (int i = 0; i < k; i++) th.emplace_back([&, i]() {
        vector<uint8_t> c; bool ok = true;
        while (qs[i].pop(c)) { size_t o = 0; while (ok && o < c.size()) { ssize_t w = write(wfd[i], c.data() + o, c.size() - o); if (w <= 0) ok = false; else o += w; } }
        close(wfd[i]);
    });
    // one pass
    vector<vector<uint8_t>> cur(k); for (int i = 0; i < k; i++) { cur[i].reserve(1 << 22); cur[i].insert(cur[i].end(), gh, gh + 24); }
    auto put = [&](int i, const uint8_t *p, size_t n) { cur[i].insert(cur[i].end(), p, p + n); if (cur[i].size() >= (1 << 22)) { qs[i].push(move(cur[i])); cur[i] = vector<uint8_t>(); cur[i].reserve(1 << 22); } };
    uint8_t tick[16 + 60] = {0}; uint32_t tl = 60; memcpy(tick + 8, &tl, 4); memcpy(tick + 12, &tl, 4);
    const uint8_t eth[14] = {2, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 2, 0x88, 0xb5}; memcpy(tick + 16, eth, 14);
    unordered_map<string, int> shardof; vector<uint64_t> load(k, 0), npk(k, 0), ntk(k, 0), last(k, ~0ull), lastus(k, 0); size_t total = 0;
    const size_t BUF = 64 << 20; vector<uint8_t> buf(BUF + (1 << 20)); size_t have = 0, pos = 0; bool eof = false;
    auto need = [&](size_t n) {   // make sure buf[pos, pos+n) is loaded
        while (have - pos < n && !eof) {
            if (pos > 0) { memmove(buf.data(), buf.data() + pos, have - pos); have -= pos; pos = 0; }
            if (have + n > buf.size()) buf.resize(have + n + (1 << 20));
            ssize_t r = read(fd, buf.data() + have, buf.size() - have); if (r <= 0) eof = true; else have += r;
        }
        return have - pos >= n;
    };
    while (need(16)) {
        uint32_t cl; memcpy(&cl, buf.data() + pos + 8, 4); if (!need(16 + (size_t)cl)) break;
        const uint8_t *h = buf.data() + pos, *b = h + 16; size_t L = cl, o = 14; string key;
        uint16_t et = L >= 14 ? (b[12] << 8 | b[13]) : 0;
        while ((et == 0x8100 || et == 0x88a8) && L >= o + 4) { et = b[o + 2] << 8 | b[o + 3]; o += 4; }
        int proto = -1; size_t l4 = 0; string a, c;
        if (et == 0x0800 && L >= o + 20) { a.assign((const char *)b + o + 12, 4); c.assign((const char *)b + o + 16, 4); proto = b[o + 9]; l4 = o + (b[o] & 15) * 4; }
        else if (et == 0x86dd && L >= o + 40) { a.assign((const char *)b + o + 8, 16); c.assign((const char *)b + o + 24, 16); proto = b[o + 6]; l4 = o + 40; }
        if (proto == 17 && L >= l4 + 4) { int sp = b[l4] << 8 | b[l4 + 1], dp = b[l4 + 2] << 8 | b[l4 + 3]; if (sp == 67 || sp == 68 || dp == 67 || dp == 68) key = "DHCP"; }
        if (key.empty()) key = proto < 0 ? string("NONIP") : min(a, c) + max(a, c);
        auto it = shardof.find(key); int dst;
        if (it == shardof.end()) { dst = 0; for (int j = 1; j < k; j++) if (load[j] < load[dst]) dst = j; shardof[key] = dst; } else dst = it->second;
        uint64_t t; memcpy(&t, h, 8); uint32_t s_, u_; memcpy(&s_, h, 4); memcpy(&u_, h + 4, 4); uint64_t us = (uint64_t)s_ * 1000000 + u_;
        put(dst, h, 16 + cl); load[dst] += 16 + cl; npk[dst]++; last[dst] = t; lastus[dst] = us; total++;
        for (int j = 0; j < k; j++) if (j != dst && last[j] != t && us - lastus[j] >= gap) { memcpy(tick, h, 8); put(j, tick, sizeof tick); last[j] = t; lastus[j] = us; ntk[j]++; }
        pos += 16 + cl;
    }
    for (int i = 0; i < k; i++) { if (!cur[i].empty()) qs[i].push(move(cur[i])); qs[i].finish(); }
    double t1 = now();
    vector<double> fin(k, 0); int left = k, bad = 0;
    while (left > 0) { int stt; pid_t w = wait(&stt); if (w < 0) break; for (int i = 0; i < k; i++) if (pid[i] == w) { fin[i] = now() - t0; left--; if (!WIFEXITED(stt) || WEXITSTATUS(stt) != 0) bad++; } }
    for (auto &x : th) x.join();
    printf("one pass: capture read and handed out in %.2f s, all Zeek done at %.2f s (%d processes, %d failed), %zu packets, %zu sets\n", t1 - t0, now() - t0, k, bad, total, shardof.size());
    for (int i = 0; i < k; i++) printf("  shard %02d: %8.1f MB, %8lu packets, %8lu ticks, done at %6.2f s\n", i, load[i] / 1e6, (unsigned long)npk[i], (unsigned long)ntk[i], fin[i]);
    return bad ? 2 : 0;
}
