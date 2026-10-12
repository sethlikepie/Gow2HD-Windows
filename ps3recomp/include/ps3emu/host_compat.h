/*
 * ps3recomp - Host platform compatibility shims
 *
 * Centralises the POSIX-flavour differences between glibc/Linux, Darwin/macOS
 * and Win32 so the HLE modules and syscall layer can stay platform-agnostic.
 *
 * Covers:
 *   - struct stat timestamp field naming (st_atim vs st_atimespec vs st_atime)
 *   - 64-bit stat and stdio file offsets (_stat64/_fseeki64 vs stat/fseeko)
 *   - pthread_mutex_timedlock (absent on Darwin)
 *   - counting semaphores (Darwin has no working unnamed POSIX semaphores)
 *   - mmap flags that only exist on Linux
 */

#ifndef PS3EMU_HOST_COMPAT_H
#define PS3EMU_HOST_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * struct stat timestamps
 *
 * Win32 exposes plain time_t seconds; Darwin uses st_*timespec; Linux and
 * other modern POSIX use st_*tim. All three collapse to seconds here, which
 * is all the CellFs / CellSaveData stat structures carry.
 * -----------------------------------------------------------------------*/
#if defined(_WIN32)
  #define PS3_STAT_ATIME_SEC(st)  ((st).st_atime)
  #define PS3_STAT_MTIME_SEC(st)  ((st).st_mtime)
  #define PS3_STAT_CTIME_SEC(st)  ((st).st_ctime)
#elif defined(__APPLE__)
  #define PS3_STAT_ATIME_SEC(st)  ((st).st_atimespec.tv_sec)
  #define PS3_STAT_MTIME_SEC(st)  ((st).st_mtimespec.tv_sec)
  #define PS3_STAT_CTIME_SEC(st)  ((st).st_ctimespec.tv_sec)
#else
  #define PS3_STAT_ATIME_SEC(st)  ((st).st_atim.tv_sec)
  #define PS3_STAT_MTIME_SEC(st)  ((st).st_mtim.tv_sec)
  #define PS3_STAT_CTIME_SEC(st)  ((st).st_ctim.tv_sec)
#endif

/* ---------------------------------------------------------------------------
 * 64-bit stat
 *
 * MSVC/MinGW need the _stat64 pair to see a file larger than 2 GB; on POSIX
 * plain stat() already carries a 64-bit off_t (always on Darwin, on Linux with
 * _FILE_OFFSET_BITS=64 or a 64-bit ABI). PS3_STAT64 names the struct and
 * ps3_stat64() the call, so a shared module writes one spelling.
 * -----------------------------------------------------------------------*/
#include <sys/stat.h>

#if defined(_WIN32)
  #define PS3_STAT64          struct _stat64
  #define ps3_stat64(p, st)   _stat64((p), (st))
#else
  #define PS3_STAT64          struct stat
  #define ps3_stat64(p, st)   stat((p), (st))
#endif

/* ---------------------------------------------------------------------------
 * 64-bit stdio file offsets
 *
 * _ftelli64 / _fseeki64 are MSVC/MinGW spellings; the POSIX equivalents are
 * ftello/fseeko. Same mapping (and same wording) as the local shims already in
 * runtime/ppu/ppu_fs.cpp and runtime/syscalls/sys_fs.c -- the #ifndef guards
 * keep this header compatible with a translation unit that also has one of
 * those in scope.
 * -----------------------------------------------------------------------*/
#if !defined(_WIN32)
  #include <stdio.h>
  #ifndef _ftelli64
    #define _ftelli64(f)          ((long long)ftello(f))
  #endif
  #ifndef _fseeki64
    #define _fseeki64(f, o, w)    fseeko((f), (off_t)(o), (w))
  #endif
#endif

/* ---------------------------------------------------------------------------
 * mmap flags
 *
 * MAP_NORESERVE is a Linux extension; Darwin and the BSDs never overcommit
 * the same way, so dropping the flag is the correct no-op.
 * -----------------------------------------------------------------------*/
#if !defined(_WIN32)
  #include <sys/mman.h>
  #ifndef MAP_NORESERVE
    #define MAP_NORESERVE 0
  #endif
#endif

#if !defined(_WIN32)

#include <pthread.h>
#include <errno.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * pthread_mutex_timedlock
 *
 * Darwin never shipped it. Spin on trylock with a short sleep; the PS3 guest
 * uses timed mutex acquisition on cold paths only, so the polling granularity
 * is not on any hot path.
 * -----------------------------------------------------------------------*/
#if defined(__APPLE__)
static inline int ps3_mutex_timedlock(pthread_mutex_t* m,
                                      const struct timespec* abstime)
{
    for (;;) {
        int rc = pthread_mutex_trylock(m);
        if (rc == 0) {
            return 0;
        }
        if (rc != EBUSY) {
            return rc;
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > abstime->tv_sec ||
            (now.tv_sec == abstime->tv_sec && now.tv_nsec >= abstime->tv_nsec)) {
            return ETIMEDOUT;
        }

        struct timespec slice = { 0, 500000 }; /* 0.5 ms */
        nanosleep(&slice, NULL);
    }
}
#else
static inline int ps3_mutex_timedlock(pthread_mutex_t* m,
                                      const struct timespec* abstime)
{
    return pthread_mutex_timedlock(m, abstime);
}
#endif

/* ---------------------------------------------------------------------------
 * Counting semaphore
 *
 * Darwin's sem_init() for unnamed semaphores is deprecated *and* non-functional
 * (it fails with ENOSYS), so a sem_timedwait() polyfill alone is not enough.
 * Build the counter out of a mutex + condvar everywhere instead of maintaining
 * two divergent code paths -- the guest-visible semantics are identical and
 * pthread_cond_timedwait gives us the timeout for free.
 * -----------------------------------------------------------------------*/
typedef struct ps3_sem {
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    unsigned        count;
} ps3_sem_t;

static inline int ps3_sem_init(ps3_sem_t* s, unsigned value)
{
    if (pthread_mutex_init(&s->mtx, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&s->cond, NULL) != 0) {
        pthread_mutex_destroy(&s->mtx);
        return -1;
    }
    s->count = value;
    return 0;
}

static inline void ps3_sem_destroy(ps3_sem_t* s)
{
    pthread_cond_destroy(&s->cond);
    pthread_mutex_destroy(&s->mtx);
}

static inline void ps3_sem_wait(ps3_sem_t* s)
{
    pthread_mutex_lock(&s->mtx);
    while (s->count == 0) {
        pthread_cond_wait(&s->cond, &s->mtx);
    }
    s->count--;
    pthread_mutex_unlock(&s->mtx);
}

/* Returns 0 on success, ETIMEDOUT on expiry. abstime is CLOCK_REALTIME. */
static inline int ps3_sem_timedwait(ps3_sem_t* s, const struct timespec* abstime)
{
    int rc = 0;
    pthread_mutex_lock(&s->mtx);
    while (s->count == 0) {
        rc = pthread_cond_timedwait(&s->cond, &s->mtx, abstime);
        if (rc != 0) {
            pthread_mutex_unlock(&s->mtx);
            return rc;
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->mtx);
    return 0;
}

/* Returns 0 on success, -1 with errno EAGAIN when the count is exhausted. */
static inline int ps3_sem_trywait(ps3_sem_t* s)
{
    pthread_mutex_lock(&s->mtx);
    if (s->count == 0) {
        pthread_mutex_unlock(&s->mtx);
        errno = EAGAIN;
        return -1;
    }
    s->count--;
    pthread_mutex_unlock(&s->mtx);
    return 0;
}

static inline void ps3_sem_getvalue(ps3_sem_t* s, int* value)
{
    pthread_mutex_lock(&s->mtx);
    *value = (int)s->count;
    pthread_mutex_unlock(&s->mtx);
}

static inline void ps3_sem_post(ps3_sem_t* s)
{
    pthread_mutex_lock(&s->mtx);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mtx);
}

#endif /* !_WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* PS3EMU_HOST_COMPAT_H */
