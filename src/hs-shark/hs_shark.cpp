// hs_shark.cpp - Harrington Sets wrapper for tshark (Wireshark): uses every core on ONE capture file.
// Original method by Michael Hugh Harrington, 2026. MIT License (full text at end of file).
//
// Wireshark dissects a capture on one thread, because packets depend on earlier packets (TCP streams,
// reassembly, request/response matching) and its state is global. hs_shark keeps dependent traffic together:
// every packet is assigned to a conversation set by its host pair (both IP addresses, either direction - this
// keeps TCP/UDP streams, IP fragments, and FTP/SIP control+data channels between the same hosts together);
// non-IP traffic forms one set. Sets are balanced across N shards by bytes (largest first), each shard is
// written as its own capture in original packet order, and N separate tshark processes dissect the shards at
// once (separate processes also avoid Wireshark's global state). The outputs are merged back into original
// packet order, and capture-wide fields are rebuilt: frame.number, frame.time_relative, frame.time_delta,
// frame.time_delta_displayed, tcp.stream and udp.stream (renumbered in order of first appearance, as tshark does).
// 1.1 (2026-10-31): --mode direct - no shard files and no output files: each tshark reads its shard from a pipe
// (tshark -r -) fed straight from the original capture, and its output is read straight into memory for the merge.
// --mode classic (default) is the original 1.0 behaviour, kept so the two can be measured against each other.
//
// Usage:  hs_shark INPUT [--jobs N] [--tshark PATH] [--editcap PATH] [--out FILE] [--tmp DIR] [--check] -- TSHARK_ARGS
//   TSHARK_ARGS must use field output: -T fields -e FIELD ... (optionally -Y FILTER, -E header=y, -E separator=X)
//   --check also runs plain single-core tshark and compares every output line.
// Example: hs_shark big.pcap --jobs 12 -- -T fields -e frame.number -e ip.src -e tcp.stream -e http.host
// Limits: display filters that use frame numbers or stream numbers (frame.number, tcp.stream ...) would select
// different packets per shard - filter on addresses/protocols instead; analyses that link different host pairs
// (e.g. DNS answer -> later connection to that address) are not combined across shards; pcapng input is
// converted to pcap with editcap first (one link type per file). Supported link types: Ethernet (with VLAN
// tags), raw IP, Linux cooked (SLL, SLL2), BSD loopback.
// Build: g++ -O2 -std=c++17 -pthread hs_shark.cpp -o hs_shark
//        zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -static hs_shark.cpp -o hs_shark.exe
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <queue>
#include <thread>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace fs = std::filesystem;
using namespace std;

static string q(const string &s) { return "\"" + s + "\""; }
static int sh(const string &cmd) {
#ifdef _WIN32
    return system(("\"" + cmd + "\"").c_str());
#else
    return system(cmd.c_str());
#endif
}
static uint32_t sw32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24); }

struct Pkt { uint64_t off; uint32_t len; int64_t tns; };   // file offset of record header, record size, time in ns
// ---- 1.1 direct mode: tshark reads its shard from a pipe (-r -) and its output is read straight into memory ----
#ifdef _WIN32
struct Child { HANDLE in = nullptr, out = nullptr, proc = nullptr; };
static mutex spawn_mu;
static Child spawn_pipe(const string &cmdline) {
    Child c; SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; HANDLE ir, iw, orr, ow;
    lock_guard<mutex> g(spawn_mu);   // child ends are inheritable only inside this lock, so no child gets another's pipe
    if (!CreatePipe(&ir, &iw, &sa, 1 << 20) || !CreatePipe(&orr, &ow, &sa, 1 << 20)) throw runtime_error("cannot create pipe");
    SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(orr, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOA si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = ir; si.hStdOutput = ow; si.hStdError = nul;
    PROCESS_INFORMATION pi{}; vector<char> b(cmdline.begin(), cmdline.end()); b.push_back(0);
    BOOL ok = CreateProcessA(nullptr, b.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(ir); CloseHandle(ow); CloseHandle(nul);
    if (!ok) { CloseHandle(iw); CloseHandle(orr); throw runtime_error("cannot start decoder: " + cmdline); }
    CloseHandle(pi.hThread); c.in = iw; c.out = orr; c.proc = pi.hProcess; return c;
}
static bool pwrite_all(Child &c, const uint8_t *p, size_t n) { while (n) { DWORD w = 0; if (!WriteFile(c.in, p, (DWORD)min<size_t>(n, 1 << 20), &w, nullptr) || !w) return false; p += w; n -= w; } return true; }
static void close_in(Child &c) { if (c.in) { CloseHandle(c.in); c.in = nullptr; } }
static size_t pread_some(Child &c, uint8_t *p, size_t n) { DWORD r = 0; if (!ReadFile(c.out, p, (DWORD)min<size_t>(n, 1 << 24), &r, nullptr)) return 0; return r; }
static int finish(Child &c) { close_in(c); CloseHandle(c.out); WaitForSingleObject(c.proc, INFINITE); DWORD code = 1; GetExitCodeProcess(c.proc, &code); CloseHandle(c.proc); return (int)code; }
#else
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
extern char **environ;
struct Child { int in = -1, out = -1; pid_t pid = 0; };
static Child spawn_pipe(const string &cmdline) {
    signal(SIGPIPE, SIG_IGN); int a[2], b[2]; if (pipe2(a, O_CLOEXEC) || pipe2(b, O_CLOEXEC)) throw runtime_error("cannot create pipe");
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa); posix_spawn_file_actions_adddup2(&fa, a[0], 0); posix_spawn_file_actions_adddup2(&fa, b[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    const char *argv[] = {"/bin/sh", "-c", cmdline.c_str(), nullptr}; Child c;
    if (posix_spawn(&c.pid, "/bin/sh", &fa, nullptr, (char **)argv, environ)) throw runtime_error("cannot start decoder: " + cmdline);
    posix_spawn_file_actions_destroy(&fa); close(a[0]); close(b[1]); c.in = a[1]; c.out = b[0]; return c;
}
static bool pwrite_all(Child &c, const uint8_t *p, size_t n) { while (n) { ssize_t w = write(c.in, p, n); if (w <= 0) return false; p += w; n -= w; } return true; }
static void close_in(Child &c) { if (c.in >= 0) { close(c.in); c.in = -1; } }
static size_t pread_some(Child &c, uint8_t *p, size_t n) { ssize_t r = read(c.out, p, n); return r > 0 ? (size_t)r : 0; }
static int finish(Child &c) { close_in(c); close(c.out); int st = 0; waitpid(c.pid, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 1; }
#endif

struct LineSrc {   // lines from a file (classic mode) or from memory (direct mode), same results as getline
    ifstream f; const vector<char> *b = nullptr; size_t pos = 0;
    bool get(string &l) { if (!b) return (bool)getline(f, l); if (pos >= b->size()) return false; const char *p = b->data() + pos, *e = b->data() + b->size();
        const char *nl = (const char *)memchr(p, '\n', e - p); size_t len = nl ? (size_t)(nl - p) : (size_t)(e - p); l.assign(p, len); pos += len + (nl ? 1 : 0); return true; }
};
// run tshark on one shard: write the pcap header + the shard's packets into its standard input, collect its output
static int run_shard_pipe(const string &cmd, const string &src, const uint8_t *gh, const vector<Pkt> &P, const vector<uint32_t> &pk, vector<char> &out) {
    Child ch = spawn_pipe(cmd);
    thread feed([&] { FILE *f = fopen(src.c_str(), "rb"); vector<uint8_t> buf(1 << 20); size_t used = 0; bool ok = f && pwrite_all(ch, gh, 24);
        for (size_t i = 0; ok && i < pk.size(); i++) { const Pkt &p = P[pk[i]]; if (used + p.len > buf.size()) { ok = pwrite_all(ch, buf.data(), used); used = 0; if (buf.size() < p.len) buf.resize(p.len); }
            if (!ok) break;
#ifdef _WIN32
            _fseeki64(f, (long long)p.off, SEEK_SET);
#else
            fseeko(f, (off_t)p.off, SEEK_SET);
#endif
            if (fread(buf.data() + used, 1, p.len, f) != p.len) { ok = false; break; } used += p.len; }
        if (ok && used) pwrite_all(ch, buf.data(), used); if (f) fclose(f); close_in(ch); });
    vector<char> b(1 << 20); for (size_t r; (r = pread_some(ch, (uint8_t *)b.data(), b.size())) > 0;) out.insert(out.end(), b.data(), b.data() + r);
    feed.join(); return finish(ch);
}


// conversation key: host pair, direction-independent
static string conv_key(int lt, const uint8_t *p, uint32_t n) {
    size_t o = 0; int et = -1;
    auto be16 = [&](size_t i) { return i + 1 < n ? (p[i] << 8) | p[i + 1] : -1; };
    if (lt == 1) { if (n < 14) return ""; et = be16(12); o = 14; while ((et == 0x8100 || et == 0x88a8 || et == 0x9100) && o + 4 <= n) { et = be16(o + 2); o += 4; } }
    else if (lt == 101 || lt == 12 || lt == 14) { if (n < 1) return ""; et = (p[0] >> 4) == 6 ? 0x86dd : 0x0800; o = 0; }
    else if (lt == 228) { et = 0x0800; o = 0; } else if (lt == 229) { et = 0x86dd; o = 0; }
    else if (lt == 113) { if (n < 16) return ""; et = be16(14); o = 16; }
    else if (lt == 276) { if (n < 20) return ""; et = be16(0); o = 20; }
    else if (lt == 0 || lt == 108) { if (n < 4) return ""; uint32_t fam = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; if (fam > 0xffff) fam = sw32(fam);
        et = fam == 2 ? 0x0800 : (fam == 24 || fam == 28 || fam == 30) ? 0x86dd : -1; o = 4; }
    else return "";
    if (et == 0x0800 && o + 20 <= n) { const uint8_t *a = p + o + 12, *b = p + o + 16; string x((const char *)a, 4), y((const char *)b, 4); return "4" + min(x, y) + max(x, y); }
    if (et == 0x86dd && o + 40 <= n) { const uint8_t *a = p + o + 8, *b = p + o + 24; string x((const char *)a, 16), y((const char *)b, 16); return "6" + min(x, y) + max(x, y); }
    return "";
}

static vector<string> split(const string &s, char sep) { vector<string> v; size_t a = 0; for (;;) { size_t b = s.find(sep, a); if (b == string::npos) { v.push_back(s.substr(a)); return v; } v.push_back(s.substr(a, b - a)); a = b + 1; } }
static string fmt_ns(int64_t ns) {   // tshark prints seconds with 9 decimals
    bool neg = ns < 0; if (neg) ns = -ns; char b[64]; snprintf(b, sizeof b, "%s%lld.%09lld", neg ? "-" : "", (long long)(ns / 1000000000), (long long)(ns % 1000000000)); return b;
}

int main(int argc, char **argv) {
    vector<string> targs; string in, out, tmp, tsh = "tshark", edc = "editcap"; int jobs = (int)thread::hardware_concurrency(); if (jobs < 1) jobs = 2; bool check = false, direct = false;
    int i = 1; for (; i < argc; i++) {
        string a = argv[i];
        if (a == "--") { for (int k = i + 1; k < argc; k++) targs.push_back(argv[k]); break; }
        else if (a == "--jobs" && i + 1 < argc) jobs = max(1, atoi(argv[++i])); else if (a == "--tshark" && i + 1 < argc) tsh = argv[++i];
        else if (a == "--editcap" && i + 1 < argc) edc = argv[++i]; else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--tmp" && i + 1 < argc) tmp = argv[++i]; else if (a == "--check") check = true;
        else if (a == "--mode" && i + 1 < argc) { string m = argv[++i]; if (m == "direct") direct = true; else if (m == "classic") direct = false; else { fprintf(stderr, "--mode must be classic or direct\n"); return 2; } }
        else if (in.empty()) in = a; else { fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (in.empty() || targs.empty()) {
        fprintf(stderr, "Harrington Sets wrapper for tshark, Michael Hugh Harrington, MIT License\n"
                        "  hs_shark INPUT [--jobs N] [--tshark PATH] [--editcap PATH] [--out FILE] [--tmp DIR] [--check] [--mode classic|direct] -- -T fields -e FIELD ...\n");
        return 2;
    }
    // inspect tshark arguments: fields, separator, quoting, header
    vector<string> fields; char sep = '\t'; char quote = 0; bool header = false, tfields = false;
    for (size_t k = 0; k < targs.size(); k++) {
        if (targs[k] == "-e" && k + 1 < targs.size()) fields.push_back(targs[k + 1]);
        if (targs[k] == "-T" && k + 1 < targs.size() && targs[k + 1] == "fields") tfields = true;
        if (targs[k] == "-E" && k + 1 < targs.size()) { string e = targs[k + 1];
            if (e.rfind("separator=", 0) == 0) { string v = e.substr(10); sep = v == "/t" ? '\t' : v == "/s" ? ' ' : v.empty() ? '\t' : v[0]; }
            if (e.rfind("quote=", 0) == 0) { char c = e.size() > 6 ? e[6] : 'n'; quote = c == 'd' ? '"' : c == 's' ? '\'' : 0; }
            if (e == "header=y") header = true; }
    }
    if (!tfields || fields.empty()) { fprintf(stderr, "error: hs_shark needs field output (-T fields -e FIELD ...)\n"); return 2; }
    if (tmp.empty()) tmp = in + ".hs_shark_tmp";
    auto t0 = chrono::steady_clock::now();
    error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp);
    try {
        // read capture (convert pcapng first)
        string src = in;
        { FILE *f = fopen(in.c_str(), "rb"); if (!f) throw runtime_error("cannot open " + in); uint32_t m = 0; if (fread(&m, 4, 1, f) != 1) m = 0; fclose(f);
          if (m == 0x0A0D0D0A) { src = (fs::path(tmp) / "input.pcap").string(); if (sh(q(edc) + " -F pcap " + q(in) + " " + q(src)) != 0) throw runtime_error("editcap could not convert the pcapng file (several link types?)"); } }
        FILE *f = fopen(src.c_str(), "rb"); uint8_t gh[24]; if (fread(gh, 1, 24, f) != 24) throw runtime_error("not a capture file");
        uint32_t magic; memcpy(&magic, gh, 4); bool swp, nano;
        if (magic == 0xa1b2c3d4) { swp = false; nano = false; } else if (magic == 0xd4c3b2a1) { swp = true; nano = false; }
        else if (magic == 0xa1b23c4d) { swp = false; nano = true; } else if (magic == 0x4d3cb2a1) { swp = true; nano = true; } else throw runtime_error("unknown capture format");
        auto r32 = [&](const uint8_t *b) { uint32_t v; memcpy(&v, b, 4); return swp ? sw32(v) : v; };
        int lt = (int)(r32(gh + 20) & 0x0fffffff);
        vector<Pkt> P; vector<int> keyid; unordered_map<string, int> kid; vector<uint64_t> kbytes; vector<uint8_t> buf(1 << 18); uint64_t off = 24; uint8_t ph[16];
        while (fread(ph, 1, 16, f) == 16) {
            uint32_t s = r32(ph), us = r32(ph + 4), incl = r32(ph + 8);
            if (incl > (1u << 28)) throw runtime_error("damaged capture (bad packet length)");
            if (buf.size() < incl) buf.resize(incl);
            if (fread(buf.data(), 1, incl, f) != incl) break;
            string k = conv_key(lt, buf.data(), incl);
            auto it = kid.find(k); int id; if (it == kid.end()) { id = (int)kbytes.size(); kid[k] = id; kbytes.push_back(0); } else id = it->second;
            kbytes[id] += 16 + incl; keyid.push_back(id);
            P.push_back({off, 16 + incl, (int64_t)s * 1000000000 + (int64_t)us * (nano ? 1 : 1000)}); off += 16 + incl;
        }
        // balance conversation sets over shards (largest first onto the lightest shard)
        int S = max(1, min(jobs, (int)kbytes.size())); vector<int> ord(kbytes.size()); for (size_t k = 0; k < ord.size(); k++) ord[k] = (int)k;
        sort(ord.begin(), ord.end(), [&](int a, int b) { return kbytes[a] > kbytes[b]; });
        vector<int> shard_of(kbytes.size()); vector<uint64_t> load(S, 0);
        for (int k : ord) { int b = (int)(min_element(load.begin(), load.end()) - load.begin()); shard_of[k] = b; load[b] += kbytes[k]; }
        vector<vector<uint64_t>> orig(S);   // shard frame -> original frame number
        vector<FILE *> sf(S); vector<string> sp(S); vector<vector<uint32_t>> pk(S);
        if (direct) { for (size_t p = 0; p < P.size(); p++) { int s = shard_of[keyid[p]]; pk[s].push_back((uint32_t)p); orig[s].push_back(p + 1); } fclose(f); }
        else {
        for (int s = 0; s < S; s++) { sp[s] = (fs::path(tmp) / ("shard" + to_string(s) + ".pcap")).string(); sf[s] = fopen(sp[s].c_str(), "wb"); if (!sf[s]) throw runtime_error("cannot write shard"); fwrite(gh, 1, 24, sf[s]); }
        rewind(f); fseek(f, 24, SEEK_SET);
        for (size_t p = 0; p < P.size(); p++) { if (buf.size() < P[p].len) buf.resize(P[p].len); if (fread(buf.data(), 1, P[p].len, f) != P[p].len) throw runtime_error("read error");
            int s = shard_of[keyid[p]]; fwrite(buf.data(), 1, P[p].len, sf[s]); orig[s].push_back(p + 1); }
        fclose(f); for (auto x : sf) fclose(x); }
        double tsplit = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        int fnreq = -1; for (size_t k = 0; k < fields.size(); k++) if (fields[k] == "frame.number") { fnreq = (int)k; break; }
        bool inject = fnreq < 0;   // tshark will not print the same field twice, so reuse the user's frame.number column
        // run tshark on every shard at once (frame.number added as the first field if not requested)
        vector<string> outp(S); vector<thread> th; vector<int> rc(S, 0);
        string a2; for (size_t k = 0; k < targs.size(); k++) { a2 += " " + q(targs[k]); if (targs[k] == "-T" && k + 1 < targs.size() && targs[k + 1] == "fields") { a2 += " " + q(targs[k + 1]) + (inject ? " -e frame.number" : ""); k++; } }
        vector<vector<char>> outb(S), mapb(S);
        if (direct) for (int s = 0; s < S; s++) th.emplace_back([&, s] { rc[s] = run_shard_pipe(q(tsh) + " -r -" + a2, src, gh, P, pk[s], outb[s]); });
        else for (int s = 0; s < S; s++) { outp[s] = sp[s] + ".txt";
#ifdef _WIN32
            string cmd = q(tsh) + " -r " + q(sp[s]) + a2 + " > " + q(outp[s]) + " 2>NUL";
#else
            string cmd = q(tsh) + " -r " + q(sp[s]) + a2 + " > " + q(outp[s]) + " 2>/dev/null";
#endif
            th.emplace_back([&, s, cmd] { rc[s] = sh(cmd); }); }
        int fn = -1, ftr = -1, ftd = -1, ftdd = -1, finfo = -1; vector<int> ts_cols, us_cols, fr_cols;
        set<string> framenum;   // fields of type FT_FRAMENUM (frame references such as http.request_in)
        { string gf = (fs::path(tmp) / "fields.txt").string();
#ifdef _WIN32
          sh(q(tsh) + " -G fields > " + q(gf) + " 2>NUL");
#else
          sh(q(tsh) + " -G fields > " + q(gf) + " 2>/dev/null");
#endif
          ifstream g(gf); string l; while (getline(g, l)) { auto c = split(l, '\t'); if (c.size() > 3 && c[0] == "F" && c[3] == "FT_FRAMENUM") framenum.insert(c[2]); } }
        for (size_t k = 0; k < fields.size(); k++) { const string &x = fields[k];
            if (x == "frame.number") fn = (int)k; else if (x == "frame.time_relative") ftr = (int)k; else if (x == "frame.time_delta") ftd = (int)k;
            else if (x == "frame.time_delta_displayed") ftdd = (int)k; else if (x == "tcp.stream") ts_cols.push_back((int)k); else if (x == "udp.stream") us_cols.push_back((int)k);
            else if (x == "_ws.col.Info" || x == "_ws.col.info") finfo = (int)k; else if (framenum.count(x)) fr_cols.push_back((int)k); }
        bool filtered = false; for (auto &x : targs) if (x == "-Y" || x == "-R") filtered = true;
        // with a display filter, tshark still numbers streams over ALL packets: run a light mapping pass per shard
        vector<string> mapp(S);
        if (filtered && (!ts_cols.empty() || !us_cols.empty())) {
            vector<thread> tm;
            if (direct) for (int s = 0; s < S; s++) { mapp[s] = "mem"; tm.emplace_back([&, s] { run_shard_pipe(q(tsh) + " -r - -T fields -e frame.number -e tcp.stream -e udp.stream", src, gh, P, pk[s], mapb[s]); }); }
            else for (int s = 0; s < S; s++) { mapp[s] = sp[s] + ".map";
#ifdef _WIN32
                string cmd = q(tsh) + " -r " + q(sp[s]) + " -T fields -e frame.number -e tcp.stream -e udp.stream > " + q(mapp[s]) + " 2>NUL";
#else
                string cmd = q(tsh) + " -r " + q(sp[s]) + " -T fields -e frame.number -e tcp.stream -e udp.stream > " + q(mapp[s]) + " 2>/dev/null";
#endif
                tm.emplace_back([cmd] { sh(cmd); }); }
            for (auto &t : tm) t.join();
        }
        for (auto &t : th) t.join();
        double tdis = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        // merge back into original order and rebuild capture-wide fields
        map<pair<int, long long>, long long> tsm, usm; long long tnext = 0, unext = 0;
        if (!mapp.empty() && !mapp[0].empty()) {   // global first-appearance numbering over every packet
            vector<LineSrc> mi(S); for (int s = 0; s < S; s++) { if (direct) mi[s].b = &mapb[s]; else mi[s].f.open(mapp[s], ios::binary); }
            struct M { uint64_t o; int s; long long t, u; bool operator>(const M &b) const { return o > b.o; } };
            priority_queue<M, vector<M>, greater<M>> mq;
            auto mpull = [&](int s) { string l; while (mi[s].get(l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); auto c = split(l, '\t'); if (c.size() < 3 || c[0].empty()) continue;
                    uint64_t n = strtoull(c[0].c_str(), nullptr, 10); if (n == 0 || n > orig[s].size()) continue;
                    mq.push({orig[s][n - 1], s, c[1].empty() ? -1 : atoll(c[1].c_str()), c[2].empty() ? -1 : atoll(c[2].c_str())}); return; } };
            for (int s = 0; s < S; s++) mpull(s);
            while (!mq.empty()) { M m = mq.top(); mq.pop();
                if (m.t >= 0 && !tsm.count({m.s, m.t})) tsm[{m.s, m.t}] = tnext++;
                if (m.u >= 0 && !usm.count({m.s, m.u})) usm[{m.s, m.u}] = unext++;
                mpull(m.s); }
        }
        vector<LineSrc> ins(S); for (int s = 0; s < S; s++) { if (direct) ins[s].b = &outb[s]; else ins[s].f.open(outp[s], ios::binary); }
        struct H { uint64_t o; int s; string rest; bool operator>(const H &b) const { return o > b.o; } };
        priority_queue<H, vector<H>, greater<H>> pq;
        int kc = inject ? 0 : fnreq;
        auto pull = [&](int s) { string l; while (ins[s].get(l)) { if (!l.empty() && l.back() == '\r') l.pop_back();
                vector<string> c = split(l, sep); if ((int)c.size() <= kc) continue;
                string a = c[kc]; if (quote && a.size() >= 2 && a.front() == quote) a = a.substr(1, a.size() - 2);
                if (a.empty() || !isdigit((unsigned char)a[0])) continue;   // header line of a shard
                uint64_t n = strtoull(a.c_str(), nullptr, 10); if (n == 0 || n > orig[s].size()) continue;
                string rest; if (inject) { size_t p0 = l.find(sep); rest = p0 == string::npos ? string() : l.substr(p0 + 1); } else rest = l;
                pq.push({orig[s][n - 1], s, rest}); return; } };
        for (int s = 0; s < S; s++) pull(s);
        FILE *o = out.empty() ? stdout : fopen(out.c_str(), "wb"); if (!o) throw runtime_error("cannot write output");
        if (header) { for (size_t k = 0; k < fields.size(); k++) fprintf(o, "%s%s", k ? string(1, sep).c_str() : "", fields[k].c_str()); fputc('\n', o); }
        int64_t lastdisp = INT64_MIN; uint64_t lines = 0;
        auto remap_frame = [&](int s, const string &v) -> string {   // shard frame numbers (comma lists) -> original numbers
            string r; for (auto &x : split(v, ',')) { if (!r.empty()) r += ","; uint64_t n = strtoull(x.c_str(), nullptr, 10);
                r += (n >= 1 && n <= orig[s].size()) ? to_string(orig[s][n - 1]) : x; } return r; };
        auto unq = [&](string v) { if (quote && v.size() >= 2 && v.front() == quote) v = v.substr(1, v.size() - 2); return v; };
        auto rq = [&](const string &v) { return quote ? string(1, quote) + v + string(1, quote) : v; };
        while (!pq.empty()) {
            H h = pq.top(); pq.pop(); vector<string> c = split(h.rest, sep); c.resize(fields.size());
            uint64_t on = h.o; int64_t t = P[on - 1].tns;
            if (fn >= 0) c[fn] = rq(to_string(on));
            if (ftr >= 0) c[ftr] = rq(fmt_ns(t - P[0].tns));
            if (ftd >= 0) c[ftd] = rq(fmt_ns(on > 1 ? t - P[on - 2].tns : 0));
            if (ftdd >= 0) { c[ftdd] = rq(fmt_ns(lastdisp == INT64_MIN ? 0 : t - lastdisp)); }
            lastdisp = t;
            for (int col : ts_cols) { string v = unq(c[col]); if (!v.empty()) { auto k = make_pair(h.s, atoll(v.c_str())); auto it = tsm.find(k); if (it == tsm.end()) it = tsm.emplace(k, tnext++).first; c[col] = rq(to_string(it->second)); } }
            for (int col : us_cols) { string v = unq(c[col]); if (!v.empty()) { auto k = make_pair(h.s, atoll(v.c_str())); auto it = usm.find(k); if (it == usm.end()) it = usm.emplace(k, unext++).first; c[col] = rq(to_string(it->second)); } }
            for (int col : fr_cols) { string v = unq(c[col]); if (!v.empty()) c[col] = rq(remap_frame(h.s, v)); }
            if (finfo >= 0) {   // frame numbers shown inside the Info column ("Dup ACK 452#1", "(reply in 30611)")
                static const char *pre[] = {"Dup ACK ", "request in ", "reply in ", "response in ", "Request in ", "Reply in ", "Response in ", "retransmission of ", "Retransmission of ", "in frame ", "frame #"};
                string v = c[finfo];
                for (const char *pf : pre) { size_t L = strlen(pf), p0 = 0;
                    while ((p0 = v.find(pf, p0)) != string::npos) { size_t a = p0 + L, b = a; while (b < v.size() && isdigit((unsigned char)v[b])) b++;
                        if (b > a) { string nv = remap_frame(h.s, v.substr(a, b - a)); v.replace(a, b - a, nv); b = a + nv.size(); } p0 = b; } }
                c[finfo] = v; }
            for (size_t k = 0; k < c.size(); k++) { if (k) fputc(sep, o); fputs(c[k].c_str(), o); } fputc('\n', o); lines++;
            pull(h.s);
        }
        if (o != stdout) fclose(o);
        double tall = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
        fprintf(stderr, "hs_shark (%s mode): %zu packets, %zu conversation sets, %d shards (%d jobs); split %.1f s, dissect %.1f s, total %.1f s, %llu lines\n",
                direct ? "direct" : "classic", P.size(), kbytes.size(), S, jobs, tsplit, tdis - tsplit, tall, (unsigned long long)lines);
        for (int s = 0; s < S; s++) if (rc[s]) fprintf(stderr, "warning: tshark returned %d on shard %d\n", rc[s], s);
        if (check && !out.empty()) {
            string plain = (fs::path(tmp) / "plain.txt").string(); auto a = chrono::steady_clock::now();
            string args; for (auto &x : targs) args += " " + q(x);
#ifdef _WIN32
            sh(q(tsh) + " -r " + q(in) + args + " > " + q(plain) + " 2>NUL");
#else
            sh(q(tsh) + " -r " + q(in) + args + " > " + q(plain) + " 2>/dev/null");
#endif
            double tp = chrono::duration<double>(chrono::steady_clock::now() - a).count();
            ifstream x(plain, ios::binary), y(out, ios::binary); string lx, ly; uint64_t n = 0, diff = 0, shown = 0;
            for (;;) { bool gx = (bool)getline(x, lx), gy = (bool)getline(y, ly); if (!gx && !gy) break; n++;
                if (!lx.empty() && lx.back() == '\r') lx.pop_back(); if (!ly.empty() && ly.back() == '\r') ly.pop_back();
                if (lx != ly || gx != gy) { diff++; if (shown++ < 5) fprintf(stderr, "  differs at line %llu:\n    plain:   %s\n    wrapper: %s\n", (unsigned long long)n, lx.c_str(), ly.c_str()); } }
            fprintf(stderr, "check: plain single-core tshark %.1f s; %llu lines compared, %llu differ (%s)\n", tp, (unsigned long long)n, (unsigned long long)diff, diff ? "NOT identical" : "IDENTICAL");
        }
    } catch (exception &e) { fs::remove_all(tmp, ec); fprintf(stderr, "error: %s\n", e.what()); return 1; }
    fs::remove_all(tmp, ec);
    return 0;
}

// MIT License
//
// Copyright (c) 2026 Michael Hugh Harrington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
