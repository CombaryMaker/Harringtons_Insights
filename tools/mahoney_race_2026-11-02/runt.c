/* runt - run a command, print wall seconds, user+sys CPU seconds and peak memory (KB) of it and its children.
   Michael Hugh Harrington benchmark helper, 2026. MIT License.  usage: runt LOGFILE cmd args...  (stderr of cmd goes to /dev/null; stdout too unless RUNT_OUT=file) */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/resource.h>
int main(int c, char **v) {
  if (c < 3) { fprintf(stderr, "usage: runt LOGFILE cmd args...\n"); return 2; }
  struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
  pid_t p = fork();
  if (p == 0) { int n = open("/dev/null", O_WRONLY); const char *o = getenv("RUNT_OUT"); int fo = o ? open(o, O_WRONLY | O_CREAT | O_TRUNC, 0644) : n; dup2(fo < 0 ? n : fo, 1); dup2(n, 2); execvp(v[2], v + 2); _exit(127); }
  int st; struct rusage ru; wait4(p, &st, 0, &ru);
  clock_gettime(CLOCK_MONOTONIC, &b);
  double w = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
  double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
  FILE *f = fopen(v[1], "w"); if (f) { fprintf(f, "%.3f %.3f %ld %d\n", w, cpu, ru.ru_maxrss, WIFEXITED(st) ? WEXITSTATUS(st) : 128); fclose(f); }
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
