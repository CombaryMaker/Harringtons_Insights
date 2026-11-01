// hs_cc.cpp (version 1.1) - compiler shim for hs_build: runs the real compiler with the same arguments (so the output is
// unchanged) and appends "start_seconds<TAB>duration_seconds<TAB>object" to the file named by HS_CC_LOG.
// Michael Hugh Harrington, 2026. MIT License.
// Use: make CC="hs_cc gcc"     Build: g++ -O2 -std=c++17 hs_cc.cpp -o hs_cc
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif
using namespace std;
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "hs_cc: usage: hs_cc COMPILER ARGS...\n"); return 2; }
    string obj; for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "-o")) obj = argv[i + 1];
    bool compile = false; for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "-c")) compile = true;
    auto t0 = chrono::steady_clock::now(); double start = chrono::duration<double>(t0.time_since_epoch()).count();
    int rc;
#ifdef _WIN32
    // _spawnvp joins the arguments with spaces WITHOUT quoting them, so an argument with a space (a path such as
    // "data compression") would be split; quote each argument by the Microsoft C runtime rules (fixed in 1.1)
    vector<string> qa; for (int i = 1; i < argc; i++) { string s = argv[i];
        if (!s.empty() && s.find_first_of(" \t\"") == string::npos) { qa.push_back(s); continue; }
        string o = "\""; size_t bs = 0;
        for (char ch : s) { if (ch == '\\') { bs++; continue; } if (ch == '"') { o.append(bs * 2 + 1, '\\'); o += '"'; } else { o.append(bs, '\\'); o += ch; } bs = 0; }
        o.append(bs * 2, '\\'); o += '"'; qa.push_back(o); }
    vector<const char *> a; for (auto &s : qa) a.push_back(s.c_str()); a.push_back(nullptr);
    rc = (int)_spawnvp(_P_WAIT, argv[1], a.data());
#else
    pid_t p = fork();
    if (p == 0) { execvp(argv[1], argv + 1); perror(argv[1]); _exit(127); }
    int st = 0; waitpid(p, &st, 0); rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
#endif
    double dur = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
    const char *log = getenv("HS_CC_LOG");
    if (log && compile && !obj.empty()) { FILE *f = fopen(log, "a"); if (f) { fprintf(f, "%.3f\t%.3f\t%s\n", start, dur, obj.c_str()); fclose(f); } }
    return rc;
}
