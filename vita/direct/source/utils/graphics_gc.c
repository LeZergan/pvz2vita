/* The linked VitaGL collector owns the purge-list cursors. Its request
 * semaphore is a queue, not a completion notification: signalling it alone
 * lets the renderer race the worker's cursor reset. Keep the existing worker
 * and purge age, but wait for every request to finish before returning to GL.
 * This also covers collection requested by the native allocation fallback. */
#include "utils/graphics_gc.h"
#include "utils/telemetry.h"
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stddef.h>

extern SceUID gc_mutex[2];
int __real_sceKernelWaitSema(SceUID, int, SceUInt *);
int __real_sceKernelSignalSema(SceUID, int);
int __real_sceKernelDelayThread(SceUInt);
extern void *gpu_alloc_mapped_aligned_unsafe(size_t, size_t, int);

int __wrap_sceKernelDelayThread(SceUInt usec) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) & ~(uintptr_t)1;
    uintptr_t recovery = (uintptr_t)&gpu_alloc_mapped_aligned_unsafe & ~(uintptr_t)1;
    /* Pinned VitaGL recovery function: the sole delay follows its GC request.
     * The signal wrapper now waits for real completion, so no guessed sleep
     * is needed. Keep every other game/SDK delay, even the same duration.
     * check-graphics-gc-arm.py verifies the native function and this call path. */
    if (usec == 1000000 && caller > recovery && caller < recovery + 0xc8)
        return 0;
    return __real_sceKernelDelayThread(usec);
}

static void __attribute__((noreturn)) gc_failure(const char *operation, int error) {
    telemetry_log("FATAL", "graphics cleanup %s failed rc=0x%x; stopping before resource reuse",
                  operation, (unsigned)error);
    /* Do not draw a dialog through a collector that has stopped responding. */
    sceKernelExitProcess(6);
    for (;;) sceKernelDelayThread(100000);
}

int pvz2_gc_wait(SceUID id, int count, SceUInt *timeout) {
    if (id > 0 && id == gc_mutex[1] && !timeout) {
        SceUInt limit = 5000000;
        int rc = __real_sceKernelWaitSema(id, count, &limit);
        if (rc < 0) gc_failure("wait", rc);
        return rc;
    }
    return __real_sceKernelWaitSema(id, count, timeout);
}

int __wrap_sceKernelSignalSema(SceUID id, int count) {
    int rc = __real_sceKernelSignalSema(id, count);
    if (id <= 0 || id != gc_mutex[0]) return rc;
    if (rc < 0) gc_failure("request", rc);

    /* Initially all maxCount tokens are available. A request consumes one;
     * the worker restores it only after resetting the shared list cursors.
     * Acquiring ALL tokens fences even a delayed or already queued worker.
     * Restore the tokens without adding a request or changing purge age. */
    static SceUID checked_id;
    static int tokens;
    if (checked_id != gc_mutex[1]) {
        SceKernelSemaInfo info = {.size = sizeof(info)};
        rc = sceKernelGetSemaInfo(gc_mutex[1], &info);
        if (rc < 0 || info.maxCount < 1 || info.maxCount > 64)
            gc_failure("configuration", rc < 0 ? rc : -1);
        tokens = info.maxCount;
        checked_id = gc_mutex[1];
    }
    SceUInt limit = 5000000;
    rc = __real_sceKernelWaitSema(gc_mutex[1], tokens, &limit);
    if (rc < 0) gc_failure("completion", rc);
    rc = __real_sceKernelSignalSema(gc_mutex[1], tokens);
    if (rc < 0) gc_failure("release", rc);
    return 0;
}
