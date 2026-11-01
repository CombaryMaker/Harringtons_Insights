/* pthread.h - POSIX threads shim over Win32 for a native Windows build of htslib's bgzip (zig cc, no winpthreads).
   Mutexes are CRITICAL_SECTIONs (recursive by nature, so PTHREAD_MUTEX_RECURSIVE works); condition variables use
   SleepConditionVariableCS, with timed waits converted from absolute CLOCK_REALTIME. No cancellation; pthread_kill
   only supports the "is it alive" probe. Written for the gzip fair-baseline test, 1 Nov 2026. MIT License,
   Michael Hugh Harrington. */
#ifndef HTS_WIN_PTHREAD_SHIM_H
#define HTS_WIN_PTHREAD_SHIM_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif
typedef struct { HANDLE h; DWORD id; } pthread_t;
typedef struct { size_t stack; } pthread_attr_t;
typedef struct { CRITICAL_SECTION cs; volatile LONG init; } pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef int pthread_condattr_t;
typedef INIT_ONCE pthread_once_t;
typedef DWORD pthread_key_t;
#define PTHREAD_MUTEX_INITIALIZER {{0}, 0}
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_DEFAULT 0
static inline int pthread_attr_init(pthread_attr_t *a) { a->stack = 0; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }
static inline int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *s) { *s = a->stack ? a->stack : (1u << 20); return 0; }
static inline int pthread_attr_setstacksize(pthread_attr_t *a, size_t s) { a->stack = s; return 0; }
typedef struct { void *(*fn)(void *); void *arg; } hts_shim_start_t;
static DWORD WINAPI hts_shim_tramp(LPVOID p) { hts_shim_start_t s = *(hts_shim_start_t *)p; free(p); s.fn(s.arg); return 0; }
static inline int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    hts_shim_start_t *s = (hts_shim_start_t *)malloc(sizeof *s); if (!s) return EAGAIN; s->fn = fn; s->arg = arg;
    t->h = CreateThread(NULL, a ? a->stack : 0, hts_shim_tramp, s, 0, &t->id); if (!t->h) { free(s); return EAGAIN; } return 0; }
static inline int pthread_join(pthread_t t, void **ret) { if (ret) *ret = NULL; WaitForSingleObject(t.h, INFINITE); CloseHandle(t.h); return 0; }
static inline int pthread_detach(pthread_t t) { CloseHandle(t.h); return 0; }
static inline pthread_t pthread_self(void) { pthread_t t; t.h = NULL; t.id = GetCurrentThreadId(); return t; }
static inline int pthread_equal(pthread_t a, pthread_t b) { return a.id == b.id; }
static inline int pthread_kill(pthread_t t, int sig) { (void)sig; DWORD c = 0; return (t.h && GetExitCodeThread(t.h, &c) && c == STILL_ACTIVE) ? 0 : ESRCH; }
static inline int pthread_mutexattr_init(pthread_mutexattr_t *a) { *a = 0; return 0; }
static inline int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { (void)a; return 0; }
static inline int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) { *a = t; return 0; }
static inline void hts_shim_lazy(pthread_mutex_t *m) {   /* static PTHREAD_MUTEX_INITIALIZER mutexes are set up on first use */
    if (m->init == 2) return;
    if (InterlockedCompareExchange(&m->init, 1, 0) == 0) { InitializeCriticalSection(&m->cs); InterlockedExchange(&m->init, 2); }
    else while (m->init != 2) Sleep(0); }
static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; InitializeCriticalSection(&m->cs); m->init = 2; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { if (m->init == 2) DeleteCriticalSection(&m->cs); m->init = 0; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { hts_shim_lazy(m); EnterCriticalSection(&m->cs); return 0; }
static inline int pthread_mutex_trylock(pthread_mutex_t *m) { hts_shim_lazy(m); return TryEnterCriticalSection(&m->cs) ? 0 : EBUSY; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { LeaveCriticalSection(&m->cs); return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; InitializeConditionVariable(c); return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { hts_shim_lazy(m); return SleepConditionVariableCS(c, &m->cs, INFINITE) ? 0 : EINVAL; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs) {
    hts_shim_lazy(m); struct timeval now; gettimeofday(&now, NULL);
    long long ms = ((long long)abs->tv_sec - now.tv_sec) * 1000 + (abs->tv_nsec / 1000000 - now.tv_usec / 1000); if (ms < 0) ms = 0;
    if (SleepConditionVariableCS(c, &m->cs, (DWORD)ms)) return 0; return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL; }
static inline int pthread_cond_signal(pthread_cond_t *c) { WakeConditionVariable(c); return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable(c); return 0; }
static BOOL CALLBACK hts_shim_once_cb(PINIT_ONCE o, PVOID p, PVOID *ctx) { (void)o; (void)ctx; ((void (*)(void))p)(); return TRUE; }
static inline int pthread_once(pthread_once_t *o, void (*fn)(void)) { return InitOnceExecuteOnce(o, hts_shim_once_cb, (PVOID)fn, NULL) ? 0 : EINVAL; }
static inline int pthread_key_create(pthread_key_t *k, void (*d)(void *)) { (void)d; *k = TlsAlloc(); return *k == TLS_OUT_OF_INDEXES ? EAGAIN : 0; }
static inline int pthread_key_delete(pthread_key_t k) { TlsFree(k); return 0; }
static inline void *pthread_getspecific(pthread_key_t k) { return TlsGetValue(k); }
static inline int pthread_setspecific(pthread_key_t k, const void *v) { return TlsSetValue(k, (LPVOID)v) ? 0 : EINVAL; }
#define pthread_cleanup_push(fn, arg) { void (*hts_shim_cfn_)(void *) = (fn); void *hts_shim_carg_ = (arg);
#define pthread_cleanup_pop(ex) if (ex) hts_shim_cfn_(hts_shim_carg_); }
#endif
