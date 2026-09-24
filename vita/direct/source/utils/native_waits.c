/* Observe actual blocking primitives, including native SDK/pixel/graphics
 * users which do not pass through the Android pthread bridge. No per-call
 * logging, allocation, timestamps, timeout changes or forced wakeups. */
#include "utils/stall_watch.h"
#include "utils/graphics_gc.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/gxm.h>
#include <pthread.h>

/* A kernel semaphore return address only identifies the SDK polling helper.
 * Retain the enclosing condition operation to distinguish its caller/owner.
 * These wrappers preserve the original primitive, arguments and result. */
#define SYNC_BEGIN(kind, cond, mutex) const int sync_slot = pvz2_stall_sync( \
    kind, (uintptr_t)(cond), (uintptr_t)(mutex), (uintptr_t)__builtin_return_address(0))
int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int __wrap_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    SYNC_BEGIN(PVZ2_SYNC_WAIT, cond, mutex);
    int rc = __real_pthread_cond_wait(cond, mutex);
    pvz2_stall_sync_done(sync_slot);
    return rc;
}
int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int __wrap_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *deadline) {
    SYNC_BEGIN(PVZ2_SYNC_TIMED_WAIT, cond, mutex);
    int rc = __real_pthread_cond_timedwait(cond, mutex, deadline);
    pvz2_stall_sync_done(sync_slot);
    return rc;
}
int __real_pthread_cond_signal(pthread_cond_t *);
int __wrap_pthread_cond_signal(pthread_cond_t *cond) {
    SYNC_BEGIN(PVZ2_SYNC_SIGNAL, cond, 0);
    int rc = __real_pthread_cond_signal(cond);
    pvz2_stall_sync_done(sync_slot);
    return rc;
}
int __real_pthread_cond_broadcast(pthread_cond_t *);
int __wrap_pthread_cond_broadcast(pthread_cond_t *cond) {
    SYNC_BEGIN(PVZ2_SYNC_BROADCAST, cond, 0);
    int rc = __real_pthread_cond_broadcast(cond);
    pvz2_stall_sync_done(sync_slot);
    return rc;
}

#if PVZ2_STRESS_READ_KIB > 0 || PVZ2_STRESS_READ_LATENCY_US > 0
/* Diagnostic scheduling/storage pressure; compiled out of release builds.
 * Delay only successful reads, preserving returned bytes, offsets and errors.
 * Each call has its own delay: this is not a global multi-reader bandwidth cap. */
static void stress_read_delay(SceSSize bytes) {
    if (bytes <= 0) return;
    uint64_t us = PVZ2_STRESS_READ_LATENCY_US;
#if PVZ2_STRESS_READ_KIB > 0
    us += ((uint64_t)bytes * 1000000u) / ((uint64_t)PVZ2_STRESS_READ_KIB * 1024u);
#endif
    while (us) {
        unsigned chunk = us > 1000000u ? 1000000u : (unsigned)us;
        sceKernelDelayThread(chunk);
        us -= chunk;
    }
}
#else
#define stress_read_delay(bytes) ((void)0)
#endif

#define OBSERVE(kind, object) const int wait_slot = pvz2_stall_native_wait(kind, (uintptr_t)(object), \
    (uintptr_t)__builtin_return_address(0))

int __real_sceKernelWaitSema(SceUID, int, SceUInt *);
int __wrap_sceKernelWaitSema(SceUID id, int count, SceUInt *timeout) {
    OBSERVE(PVZ2_NATIVE_SEMA, id);
    int rc = pvz2_gc_wait(id, count, timeout);
    pvz2_stall_native_done(wait_slot);
    return rc;
}
int __real_sceKernelLockMutex(SceUID, int, SceUInt *);
int __wrap_sceKernelLockMutex(SceUID id, int count, SceUInt *timeout) {
    OBSERVE(PVZ2_NATIVE_MUTEX, id);
    int rc = __real_sceKernelLockMutex(id, count, timeout);
    pvz2_stall_native_done(wait_slot);
    return rc;
}
void __real_sceGxmFinish(SceGxmContext *);
void __wrap_sceGxmFinish(SceGxmContext *ctx) {
    OBSERVE(PVZ2_NATIVE_GPU, ctx);
    __real_sceGxmFinish(ctx);
    pvz2_stall_native_done(wait_slot);
}
SceSSize __real_sceIoRead(SceUID, void *, SceSize);
SceSSize __wrap_sceIoRead(SceUID fd, void *buf, SceSize size) {
    OBSERVE(PVZ2_NATIVE_READ, fd);
    SceSSize rc = __real_sceIoRead(fd, buf, size);
    stress_read_delay(rc);
    pvz2_stall_native_done(wait_slot);
    return rc;
}
int __real_sceIoPread(SceUID, void *, SceSize, SceOff);
int __wrap_sceIoPread(SceUID fd, void *buf, SceSize size, SceOff offset) {
    OBSERVE(PVZ2_NATIVE_PREAD, fd);
    int rc = __real_sceIoPread(fd, buf, size, offset);
    stress_read_delay(rc);
    pvz2_stall_native_done(wait_slot);
    return rc;
}
