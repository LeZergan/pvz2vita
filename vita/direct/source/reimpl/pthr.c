/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022      GrapheneCt
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/pthr.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/error.h>
#include <sys/time.h>
#include <stdatomic.h>

#include "utils/utils.h"
#include "utils/logger.h"
#include "utils/telemetry.h"

#define PTHR_MAX_OBJECTS 8192u
#define BIONIC_PTHREAD_COND_INITIALIZER 0
#define BIONIC_PTHREAD_MUTEX_INITIALIZER 0
#define BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER 0x4000
#define BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER 0x8000
#define PTHR_INLINE static inline __attribute__((always_inline))

/* Published keys certify that real_ptr has been initialized. Readers take no
 * global lock. Init/destroy serialize rare mutations. A reader that misses during
 * backshift deletion retries under the writer lock before initialization. */
static atomic_uintptr_t initializedObjects[PTHR_MAX_OBJECTS];
static pthread_mutex_t pthr_mutex = PTHREAD_MUTEX_INITIALIZER;
#define PTHR_LOCK pthread_mutex_lock(&pthr_mutex);
#define PTHR_UNLOCK pthread_mutex_unlock(&pthr_mutex);
static unsigned object_hash(const void *p) {
    uintptr_t v = (uintptr_t)p >> 2;
    v ^= v >> 16;
    return (unsigned)(v * 2654435761u) & (PTHR_MAX_OBJECTS - 1);
}
static int object_slot(const void *p) {
    unsigned start = object_hash(p);
    for (unsigned i = 0; i < PTHR_MAX_OBJECTS; ++i) {
        unsigned at = (start + i) & (PTHR_MAX_OBJECTS - 1);
        uintptr_t key = atomic_load_explicit(&initializedObjects[at], memory_order_acquire);
        if (key == (uintptr_t)p) return (int)at;
        if (!key) break;
    }
    return -1;
}
static int isObjectInitialized_nl(const void *p) { return p && object_slot(p) >= 0; }
static int rememberObject_nl(void *p) {
    unsigned start = object_hash(p);
    for (unsigned i = 0; i < PTHR_MAX_OBJECTS; ++i) {
        unsigned at = (start + i) & (PTHR_MAX_OBJECTS - 1);
        uintptr_t key = atomic_load_explicit(&initializedObjects[at], memory_order_relaxed);
        if (key == (uintptr_t)p) return 1;
        if (!key) {
            atomic_store_explicit(&initializedObjects[at], (uintptr_t)p, memory_order_release);
            return 1;
        }
    }
    return 0;
}
static void remove_slot(unsigned hole) {
    const unsigned mask = PTHR_MAX_OBJECTS - 1;
    const unsigned start = hole;
    for (unsigned step = 1; step < PTHR_MAX_OBJECTS; ++step) {
        unsigned at = (start + step) & mask;
        uintptr_t key = atomic_load_explicit(&initializedObjects[at], memory_order_relaxed);
        if (!key) break;
        unsigned home = object_hash((const void *)key);
        if (((hole - home) & mask) < ((at - home) & mask)) {
            atomic_store_explicit(&initializedObjects[hole], key, memory_order_release);
            hole = at;
        }
    }
    atomic_store_explicit(&initializedObjects[hole], 0, memory_order_release);
}
int isObjectInitialized(const void *p) { return isObjectInitialized_nl(p); }
int rememberObject(void *p) {
    PTHR_LOCK
    int r = isObjectInitialized_nl(p) || rememberObject_nl(p);
    PTHR_UNLOCK
    return r;
}
int forgetObject(const void *p) {
    PTHR_LOCK
    int slot = p ? object_slot(p) : -1;
    if (slot >= 0) remove_slot((unsigned)slot);
    PTHR_UNLOCK
    return slot >= 0;
}

// null check for `attr` must be performed before this
PTHR_INLINE int _attr_t_static_init(pthread_attr_t_bionic * attr) {
    if (attr->magic != 0x42424242) {
        attr->real_ptr = malloc(sizeof(pthread_attr_t));
        if (!attr->real_ptr) return ENOMEM;
        int rc = pthread_attr_init(attr->real_ptr);
        if (rc) { free(attr->real_ptr); attr->real_ptr = NULL; return rc; }
        attr->magic = 0x42424242;
    }
    return 0;
}

// null check for `mutex` param must be performed before this, `attr` is fine as null
PTHR_INLINE int _mutex_t_static_init(pthread_mutex_t_bionic * mutex, const pthread_mutexattr_t * attr) {
    if (isObjectInitialized_nl(mutex)) return 0;
    int ret = 0, kind = PTHREAD_MUTEX_NORMAL;

    /* Hold the lock across the ENTIRE check-init-remember so two threads can't
     * both initialize the same statically-initialized mutex (which produced two
     * real mutexes and a missed-wakeup race). */
    PTHR_LOCK
    if (isObjectInitialized_nl(mutex)) {
        PTHR_UNLOCK
        return ret;
    }

    if (attr) {
        pthread_mutexattr_gettype((pthread_mutexattr_t *) attr, &kind);
    } else {
        if (* (int *) mutex == BIONIC_PTHREAD_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_NORMAL;
        else if (* (int *) mutex == BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_RECURSIVE;
        else if (* (int *) mutex == BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_ERRORCHECK;
    }

    mutex->real_ptr = malloc(sizeof(pthread_mutex_t));
    if (!mutex->real_ptr) {
        PTHR_UNLOCK
        return ENOMEM;
    }
    sceClibMemset(mutex->real_ptr, 0, sizeof(pthread_mutex_t));

    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_settype(&mutattr, kind);
    ret = pthread_mutex_init(mutex->real_ptr, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    if (!ret && !rememberObject_nl(mutex)) {
        pthread_mutex_destroy(mutex->real_ptr);
        ret = EAGAIN;
    }
    if (ret) { free(mutex->real_ptr); mutex->real_ptr = NULL; }

    PTHR_UNLOCK
    return ret;
}

// null check for `cond` param must be performed before this, `attr` is fine as null
PTHR_INLINE int _cond_t_static_init(pthread_cond_t_bionic * cond, const pthread_condattr_t * attr) {
    if (isObjectInitialized_nl(cond)) return 0;
    int ret = 0;

    /* Atomic check-init-remember (see _mutex_t_static_init) to avoid two
     * threads creating two real conds for the same statically-initialized cond,
     * which caused signals/waits to target different conds → missed wakeup. */
    PTHR_LOCK
    if (isObjectInitialized_nl(cond)) {
        PTHR_UNLOCK
        return ret;
    }

    cond->real_ptr = malloc(sizeof(pthread_cond_t));
    if (!cond->real_ptr) {
        PTHR_UNLOCK
        return ENOMEM;
    }
    sceClibMemset(cond->real_ptr, 0, sizeof(pthread_cond_t));

    ret = pthread_cond_init(cond->real_ptr, attr);

    if (!ret && !rememberObject_nl(cond)) {
        pthread_cond_destroy(cond->real_ptr);
        ret = EAGAIN;
    }
    if (ret) { free(cond->real_ptr); cond->real_ptr = NULL; }

    PTHR_UNLOCK
    return ret;
}

/* Keep game/render on user core 0 and schedule native workers on cores 1-2.
 * An already-installed core-3 unlock may widen workers; failure falls back.
 * Resolve once on main before constructors can create concurrent workers. */
static atomic_int g_core3_mask = ATOMIC_VAR_INIT(0x00060000);
#define WORKER_STATS_CAP 32
static atomic_int worker_stats_ids[WORKER_STATS_CAP];
/* Read only once per telemetry interval, never on a game mutex operation. */
void pvz2_threads_format_stats(char *out, size_t size) {
    static int previous_ids[WORKER_STATS_CAP];
    static uint64_t previous_ticks[WORKER_STATS_CAP], main_ticks;
    uint64_t workers_delta = 0, main_delta = 0;
    unsigned active = 0, masks = 0, cores = 0, advanced = 0, misplaced = 0;
    SceKernelThreadInfo info = { .size = sizeof(info) };
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) >= 0) {
        if (main_ticks && info.runClocks >= main_ticks) main_delta = info.runClocks - main_ticks;
        main_ticks = info.runClocks;
    }
    for (unsigned i = 0; i < WORKER_STATS_CAP; ++i) {
        int tid = atomic_load(&worker_stats_ids[i]);
        if (tid <= 0) { previous_ids[i] = 0; continue; }
        info.size = sizeof(info);
        if (sceKernelGetThreadInfo(tid, &info) < 0) {
            atomic_compare_exchange_strong(&worker_stats_ids[i], &tid, 0);
            previous_ids[i] = 0;
            continue;
        }
        ++active;
        masks |= info.currentCpuAffinityMask;
        if (!(info.currentCpuAffinityMask & atomic_load(&g_core3_mask)) ||
            (info.currentCpuAffinityMask & 0x10000)) ++misplaced;
        if ((unsigned)info.lastExecutedCpuId < 4) cores |= 1u << info.lastExecutedCpuId;
        if (previous_ids[i] == tid && info.runClocks >= previous_ticks[i]) {
            workers_delta += info.runClocks - previous_ticks[i];
            if (info.runClocks > previous_ticks[i]) ++advanced;
        }
        previous_ids[i] = tid;
        previous_ticks[i] = info.runClocks;
    }
    snprintf(out, size, "native_active=%u ran=%u misplaced=%u masks=0x%x last_cores=0x%x main_ticks=%llu worker_ticks=%llu",
             active, advanced, misplaced, masks, cores, (unsigned long long)main_delta, (unsigned long long)workers_delta);
}
void pvz2_init_thread_affinity(void) {
    SceUID self = sceKernelGetThreadId();
    int workers = 0x00060000;
    /* Probe an already-unlocked fourth core; ordinary hardware rejects this.
     * Verify actual affinity before advertising the extra worker core. */
    if (sceKernelChangeThreadCpuAffinityMask(self, 0x000E0000) >= 0 &&
        sceKernelGetThreadCpuAffinityMask(self) == 0x000E0000)
        workers = 0x000E0000;
    atomic_store(&g_core3_mask, workers);
    int rc = sceKernelChangeThreadCpuAffinityMask(self, 0x00010000);
    telemetry_log("CPU", "main mask=0x%05x rc=0x%08x; workers mask=0x%05x",
                  sceKernelGetThreadCpuAffinityMask(self), (unsigned)rc, workers);
}
int pvz2_cpu_core_count(void) {
    return (atomic_load(&g_core3_mask) & 0x80000) ? 4 : 3;
}

typedef struct { void *(*start)(void *); void *param; } mcsm_thr_wrap;
static int worker_apply_affinity(SceUID self) {
    int wanted = atomic_load(&g_core3_mask);
    int rc = sceKernelChangeThreadCpuAffinityMask(self, wanted);
    /* Verify actual state as well as the return code. If a widened mask fails,
     * keep trying ordinary worker cores before accepting inherited core 0. */
    int actual = sceKernelGetThreadCpuAffinityMask(self);
    if (rc >= 0 && actual == wanted) return 0;
    const int fallback[] = {0x60000, 0x20000, 0x40000};
    for (unsigned i = 0; i < sizeof(fallback)/sizeof(fallback[0]); ++i) {
        rc = sceKernelChangeThreadCpuAffinityMask(self, fallback[i]);
        actual = sceKernelGetThreadCpuAffinityMask(self);
        if (rc >= 0 && actual == fallback[i]) return 0;
    }
    return rc < 0 ? rc : -1;
}
static void *mcsm_thr_entry(void *arg) {
    mcsm_thr_wrap *w = (mcsm_thr_wrap *)arg;
    void *(*start)(void *) = w->start;
    void *param = w->param;
    free(w);
    SceUID self = sceKernelGetThreadId();
    int rc = worker_apply_affinity(self);
    if (rc < 0) telemetry_log("CPU_ERROR", "worker tid=0x%x could not leave inherited affinity rc=0x%x", self, (unsigned)rc);
    static atomic_uint logged = ATOMIC_VAR_INIT(0);
    if (atomic_fetch_add(&logged, 1) < 24)
        telemetry_log("CPU", "worker tid=0x%x mask=0x%05x rc=0x%08x",
                      self, sceKernelGetThreadCpuAffinityMask(self), (unsigned)rc);
    int slot = -1;
    for (unsigned i = 0; i < WORKER_STATS_CAP; ++i) {
        int empty = 0;
        if (atomic_compare_exchange_strong(&worker_stats_ids[i], &empty, self)) {
            slot = (int)i;
            break;
        }
    }
    void *result = start(param);
    if (slot >= 0) atomic_store(&worker_stats_ids[slot], 0);
    return result;
}

int pthread_create_soloader(pthread_t *thread, const pthread_attr_t_bionic *attr, void *(*start)(void *), void *param) {
    int ret;
    const size_t default_stack = 128 * 1024;
    const size_t min_stack = 64 * 1024;
    size_t requested_stack = 0;
    size_t stack_to_use = default_stack;
    pthread_attr_t local_attr;
    pthread_attr_t *real_attr = NULL;

    if (!thread || !start) {
        return EINVAL;
    }

    if (!attr) {
        ret = pthread_attr_init(&local_attr);
        if (ret) return ret;
        real_attr = &local_attr;
    } else {
        ret = _attr_t_static_init((pthread_attr_t_bionic *)attr);
        if (ret) return ret;
        real_attr = attr->real_ptr;
    }

    if (real_attr && pthread_attr_getstacksize(real_attr, &requested_stack) != 0) {
        requested_stack = 0;
    }

    if (requested_stack >= min_stack && requested_stack <= default_stack) {
        stack_to_use = requested_stack;
    }

    if (real_attr) {
        pthread_attr_setstacksize(real_attr, stack_to_use);
    }

    /* Every successful creation must pass through affinity setup. Returning
     * ENOMEM is preferable to silently running a worker on the rendering core. */
    void *(*entry)(void *) = start;
    void *entry_arg = param;
    mcsm_thr_wrap *wrap = (mcsm_thr_wrap *)malloc(sizeof(mcsm_thr_wrap));
    if (!wrap) {
        if (!attr) pthread_attr_destroy(&local_attr);
        return ENOMEM;
    }
    if (wrap) {
        wrap->start = start;
        wrap->param = param;
        entry = mcsm_thr_entry;
        entry_arg = wrap;
    }

    ret = pthread_create(thread, real_attr, entry, entry_arg);
    if (ret != 0 && real_attr && stack_to_use > min_stack) {
        pthread_attr_setstacksize(real_attr, min_stack);
        stack_to_use = min_stack;
        ret = pthread_create(thread, real_attr, entry, entry_arg);
        l_warn("pthread_create retry with %u KB stack -> ret=%d", (unsigned)(min_stack / 1024), ret);
    }

    if (ret == EAGAIN && real_attr) {
        // Resource pressure can be transient while worker threads bootstrap.
        for (int i = 0; i < 8 && ret == EAGAIN; ++i) {
            sceKernelDelayThread(20000);
            ret = pthread_create(thread, real_attr, entry, entry_arg);
            if (ret == 0) {
                l_warn("pthread_create recovered after %d EAGAIN retries", i + 1);
                break;
            }
        }
    }

    /* Reclaim the wrapper ONLY if no attempt ever started the thread; on
     * success the new thread owns it and frees it in mcsm_thr_entry. */
    if (ret != 0 && wrap && entry == mcsm_thr_entry) {
        free(wrap);
    }

    /* On success the worker may already have freed param; never inspect it. */
    if (ret != 0) {
        l_warn("pthread_create(start=%p, stack=%u KB) failed ret=%d",
               start, (unsigned)(stack_to_use / 1024), ret);
    }

    if (!attr) {
        pthread_attr_destroy(&local_attr);
    }

    return ret;
}

int pthread_mutexattr_init_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_init(attr);
}

int pthread_mutexattr_settype_soloader(pthread_mutexattr_t *attr, int type)
{
    return pthread_mutexattr_settype(attr, type);
}

int pthread_mutexattr_destroy_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_destroy(attr);
}

int pthread_kill_soloader(pthread_t thread, int sig)
{
    return pthread_kill(thread, sig);
}

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *uid, const pthread_mutexattr_t *attr)
{
    if (!uid) return EINVAL;
    return _mutex_t_static_init(uid, attr);
}

int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *object) {
    if (!object) return EINVAL;
    PTHR_LOCK
    int slot = object_slot(object), rc = 0;
    if (slot >= 0) {
        rc = pthread_mutex_destroy(object->real_ptr);
        if (!rc) {
            remove_slot((unsigned)slot);
            free(object->real_ptr);
            object->real_ptr = NULL;
        }
    } else object->real_ptr = NULL;
    PTHR_UNLOCK
    return rc;
}

int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    int rc = _mutex_t_static_init(mutex, NULL);
    if (rc) return rc;
    return pthread_mutex_lock(mutex->real_ptr);
}

int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    int rc = _mutex_t_static_init(mutex, NULL);
    if (rc) return rc;
    return pthread_mutex_trylock(mutex->real_ptr);
}

int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    if (!mutex->real_ptr) return EINVAL;
    return pthread_mutex_unlock(mutex->real_ptr);
}

int pthread_join_soloader(pthread_t thread, void **value_ptr)
{
    return pthread_join(thread, value_ptr);
}

int pthread_condattr_init_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_init(attr);
}

int pthread_condattr_destroy_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_destroy(attr);
}

int pthread_cond_init_soloader(pthread_cond_t_bionic *cond,
                               const pthread_condattr_t *attr)
{
    if (!cond) return EINVAL;

    return _cond_t_static_init(cond, attr);
}

int pthread_cond_destroy_soloader(pthread_cond_t_bionic *object) {
    if (!object) return EINVAL;
    PTHR_LOCK
    int slot = object_slot(object), rc = 0;
    if (slot >= 0) {
        rc = pthread_cond_destroy(object->real_ptr);
        if (!rc) {
            remove_slot((unsigned)slot);
            free(object->real_ptr);
            object->real_ptr = NULL;
        }
    } else object->real_ptr = NULL;
    PTHR_UNLOCK
    return rc;
}

int pthread_cond_signal_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    int rc = _cond_t_static_init(cond, NULL);
    if (rc) return rc;

    return pthread_cond_signal(cond->real_ptr);
}

int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex, struct timespec *abstime)
{
    if (!cond || !mutex) return EINVAL;

    int rc = _cond_t_static_init(cond, NULL);
    if (!rc) rc = _mutex_t_static_init(mutex, NULL);
    if (rc) return rc;

    return pthread_cond_timedwait(cond->real_ptr, mutex->real_ptr, abstime);
}


int pthread_cond_wait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex)
{
    if (!cond || !mutex) return EINVAL;

    int rc = _cond_t_static_init(cond, NULL);
    if (!rc) rc = _mutex_t_static_init(mutex, NULL);
    if (rc) return rc;

    return pthread_cond_wait(cond->real_ptr, mutex->real_ptr);
}

int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    int rc = _cond_t_static_init(cond, NULL);
    if (rc) return rc;

    return pthread_cond_broadcast(cond->real_ptr);
}

int pthread_attr_init_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return EINVAL;

    return _attr_t_static_init(attr);
}

int pthread_attr_destroy_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return 0;
    if (attr->magic != 0x42424242) return 0;

    int ret = pthread_attr_destroy(attr->real_ptr);
    free(attr->real_ptr);
    attr->magic = 0x0;

    return ret;
}

int pthread_attr_setdetachstate_soloader(pthread_attr_t_bionic *attr, int state)
{
    if (!attr) return -1;
    int rc = _attr_t_static_init(attr);
    if (rc) return rc;
    state = !state; // pthread-embedded has JOINABLE/DETACHED swapped compared to BIONIC...
    return pthread_attr_setdetachstate(attr->real_ptr, state);
}

int pthread_attr_setstacksize_soloader(pthread_attr_t_bionic *attr, size_t stacksize) {
    if (!attr) return -1;
    int rc = _attr_t_static_init(attr);
    if (rc) return rc;
    return pthread_attr_setstacksize(attr->real_ptr, stacksize);
}

int pthread_setschedparam_soloader(pthread_t thread, int policy,
                                   const struct sched_param *param)
{
   return pthread_setschedparam(thread, policy, param);
}

int pthread_getschedparam_soloader(pthread_t thread, int *policy,
                                   struct sched_param *param)
{
    return pthread_getschedparam(thread, policy, param);
}

int pthread_detach_soloader(pthread_t thread)
{
    return pthread_detach(thread);
}

int pthread_equal_soloader(const pthread_t t1, const pthread_t t2)
{
    if (t1 == t2)
        return 1;
    if (!t1 || !t2)
        return 0;
    return pthread_equal(t1, t2);
}

pthread_t pthread_self_soloader()
{
    return pthread_self();
}

int pthread_once_soloader(volatile int *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) {
        return EINVAL;
    }

    int state = __atomic_load_n(once_control, __ATOMIC_ACQUIRE);
    if (state == 2) {
        return 0;
    }

    if (state == 0 && __sync_bool_compare_and_swap(once_control, 0, 1)) {
        init_routine();
        __atomic_store_n(once_control, 2, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(once_control, __ATOMIC_ACQUIRE) == 1) {
        sceKernelDelayThread(1000);
    }

    return 0;
}

#ifndef MAX_TASK_COMM_LEN
#define MAX_TASK_COMM_LEN 16
#endif

int pthread_setname_np_soloader(pthread_t thread, const char* thread_name) {
    if (thread == 0 || thread_name == NULL) {
        return EINVAL;
    }
    size_t thread_name_len = strlen(thread_name);
    if (thread_name_len >= MAX_TASK_COMM_LEN) {
        return ERANGE;
    }

    sceClibPrintf("PTHREAD: pthread_setname_np with name %s for thread:0x%x\n", thread_name, pthread_self());

    return 0;
}

int sem_destroy_soloader(int * uid) {
    if (sceKernelDeleteSema(*uid) < 0)
        return -1;
    return 0;
}

int sem_getvalue_soloader (int * uid, int * sval) {
    if (!uid || !sval) {
        errno = EINVAL;
        return -1;
    }

    SceKernelSemaInfo info;
    info.size = sizeof(SceKernelSemaInfo);

    if (sceKernelGetSemaInfo(*uid, &info) < 0) return -1;
    *sval = info.currentCount;
    return 0;
}

int sem_init_soloader (int * uid, int pshared, unsigned int value) {
    *uid = sceKernelCreateSema("sema", 0, (int) value, 0x7fffffff, NULL);
    if (*uid < 0)
        return -1;
    return 0;
}

int sem_post_soloader (int * uid) {
    if (sceKernelSignalSema(*uid, 1) < 0)
        return -1;
    return 0;
}

int sem_timedwait_soloader (int * uid, const struct timespec * abstime) {
    if (!uid) {
        errno = EINVAL;
        return -1;
    }

    /* POSIX permits immediate acquisition even with an expired deadline. */
    uint timeout = 0;
    int rc = sceKernelWaitSema(*uid, 1, &timeout);
    if (rc >= 0)
        return 0;
    if (rc != (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT) {
        errno = rc == (int)SCE_KERNEL_ERROR_WAIT_CANCEL ? EINTR : EINVAL;
        return -1;
    }
    if (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    /* time_t is 32-bit on Vita: promote BEFORE multiplying epoch seconds.
     * The old product overflowed and reduced future deadlines to a 1ms poll. */
    int64_t deadline = (int64_t)abstime->tv_sec * INT64_C(1000000) +
                       (abstime->tv_nsec + 999) / 1000;
    for (;;) {
        struct timeval now;
        if (gettimeofday(&now, NULL) < 0) return -1;
        int64_t remaining = deadline - ((int64_t)now.tv_sec * INT64_C(1000000) + now.tv_usec);
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        timeout = remaining > UINT32_MAX ? UINT32_MAX : (uint)remaining;
        rc = sceKernelWaitSema(*uid, 1, &timeout);
        if (rc >= 0) return 0;
        if (rc != (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT) {
            errno = rc == (int)SCE_KERNEL_ERROR_WAIT_CANCEL ? EINTR : EINVAL;
            return -1;
        }
    }
}

int sem_trywait_soloader (int * uid) {
    if (!uid) {
        errno = EINVAL;
        return -1;
    }

    uint timeout = 0;
    if (sceKernelWaitSema(*uid, 1, &timeout) < 0) {
        errno = EAGAIN;
        return -1;
    }

    return 0;
}

int sem_wait_soloader (int * uid) {
    if (!uid) {
        errno = EINVAL;
        return -1;
    }

    if (sceKernelWaitSema(*uid, 1, NULL) < 0) {
        errno = EINTR;
        return -1;
    }

    return 0;
}
