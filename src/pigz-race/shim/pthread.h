/* pthread.h - minimal POSIX threads shim over the Win32 API, just enough for pigz (yarn.c, try.c).
   Written for the pigz vs Harrington Sets test, 31 Oct 2026. MIT License, Michael Hugh Harrington.
   Not a general pthreads implementation: no cancellation, no attributes beyond joinable. */
#ifndef PIGZ_WIN_PTHREAD_SHIM_H
#define PIGZ_WIN_PTHREAD_SHIM_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <errno.h>

typedef struct { HANDLE h; DWORD id; } pthread_t;
typedef int pthread_attr_t;
typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef INIT_ONCE pthread_once_t;
typedef DWORD pthread_key_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
#define PTHREAD_CREATE_JOINABLE 0

static inline int pthread_attr_init(pthread_attr_t *a) { *a = 0; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s) { (void)a; (void)s; return 0; }

typedef struct { void *(*fn)(void *); void *arg; } pthread_shim_start_t;
static DWORD WINAPI pthread_shim_tramp(LPVOID p) {
    pthread_shim_start_t s = *(pthread_shim_start_t *)p; free(p); s.fn(s.arg); return 0;
}
static inline int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    (void)a; pthread_shim_start_t *s = (pthread_shim_start_t *)malloc(sizeof *s); if (!s) return EAGAIN;
    s->fn = fn; s->arg = arg; t->h = CreateThread(NULL, 0, pthread_shim_tramp, s, 0, &t->id);
    if (!t->h) { free(s); return EAGAIN; } return 0;
}
static inline int pthread_join(pthread_t t, void **ret) {
    if (ret) *ret = NULL; WaitForSingleObject(t.h, INFINITE); CloseHandle(t.h); return 0;
}
static inline pthread_t pthread_self(void) { pthread_t t; t.h = NULL; t.id = GetCurrentThreadId(); return t; }
static inline int pthread_equal(pthread_t a, pthread_t b) { return a.id == b.id; }

static inline int pthread_mutex_init(pthread_mutex_t *m, const void *a) { (void)a; InitializeSRWLock(m); return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive(m); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive(m); return 0; }

static inline int pthread_cond_init(pthread_cond_t *c, const void *a) { (void)a; InitializeConditionVariable(c); return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : EINVAL; }
static inline int pthread_cond_signal(pthread_cond_t *c) { WakeConditionVariable(c); return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable(c); return 0; }

static BOOL CALLBACK pthread_shim_once_cb(PINIT_ONCE o, PVOID p, PVOID *ctx) { (void)o; (void)ctx; ((void (*)(void))p)(); return TRUE; }
static inline int pthread_once(pthread_once_t *o, void (*fn)(void)) { return InitOnceExecuteOnce(o, pthread_shim_once_cb, (PVOID)fn, NULL) ? 0 : EINVAL; }

static inline int pthread_key_create(pthread_key_t *k, void (*d)(void *)) { (void)d; *k = TlsAlloc(); return *k == TLS_OUT_OF_INDEXES ? EAGAIN : 0; }
static inline void *pthread_getspecific(pthread_key_t k) { return TlsGetValue(k); }
static inline int pthread_setspecific(pthread_key_t k, const void *v) { return TlsSetValue(k, (LPVOID)v) ? 0 : EINVAL; }

/* no cancellation: the cleanup handler runs at pop when its argument is nonzero */
#define pthread_cleanup_push(fn, arg) { void (*pthread_shim_cfn_)(void *) = (fn); void *pthread_shim_carg_ = (arg);
#define pthread_cleanup_pop(ex) if (ex) pthread_shim_cfn_(pthread_shim_carg_); }
#endif
