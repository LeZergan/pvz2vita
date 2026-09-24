/* Low-frequency hardware diagnostics. No allocation, stack scanning, forced
 * wakeups, or clocks in mutex/render calls. The observer never takes a game lock. */
#include "utils/stall_watch.h"
#include "utils/bounded_log.h"
#include "utils/telemetry.h"
#include <stdatomic.h>
#include <psp2/kernel/threadmgr.h>

#define WATCH_SLOTS 40
typedef struct {
    atomic_int tid;
    atomic_uint kind, object, caller;
    atomic_uint native_kind, native_object, native_caller;
} WaitSlot;
static WaitSlot slots[WATCH_SLOTS];
typedef struct {
    atomic_uint kind, condition, mutex, caller;
} SyncSlot;
static SyncSlot sync_slots[WATCH_SLOTS];
static _Thread_local int own_slot = -1;
static atomic_uint main_frame, main_phase;
static int main_tid;

/* Native waits can occur while libc is allocating emulated TLS. Never touch a
 * _Thread_local variable here: doing so could recurse through malloc's lock. */
static int native_slot(int create) {
    int tid = sceKernelGetThreadId();
    for (unsigned i = 0; i < WATCH_SLOTS; ++i)
        if (atomic_load_explicit(&slots[i].tid, memory_order_acquire) == tid) return (int)i;
    if (!create) return -1;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < WATCH_SLOTS; ++i) {
            int previous = atomic_load_explicit(&slots[i].tid, memory_order_acquire);
            if (previous < 0) continue;
            if (previous > 0) {
                if (!pass) continue;
                SceKernelThreadInfo info = {.size = sizeof(info)};
                if (sceKernelGetThreadInfo(previous, &info) >= 0) continue;
            }
            if (!atomic_compare_exchange_strong(&slots[i].tid, &previous, -1)) continue;
            atomic_store(&slots[i].kind, PVZ2_WAIT_NONE);
            atomic_store(&slots[i].native_kind, PVZ2_NATIVE_NONE);
            atomic_store(&sync_slots[i].kind, PVZ2_SYNC_NONE);
            atomic_store(&slots[i].tid, tid);
            return (int)i;
        }
    }
    return -1;
}

void pvz2_stall_frame(unsigned frame, unsigned phase) {
    if (!pvz2_logging_enabled) return;
    atomic_store_explicit(&main_phase, phase, memory_order_relaxed);
    atomic_store_explicit(&main_frame, frame, memory_order_release);
}
static void reserve_slot(void) {
    if (own_slot < 0) own_slot = native_slot(1);
}
void pvz2_stall_wait(unsigned kind, uintptr_t object, uintptr_t caller) {
    if (!pvz2_logging_enabled) return;
    reserve_slot();
    if (own_slot < 0) return;
    WaitSlot *s = &slots[own_slot];
    atomic_store_explicit(&s->object, object, memory_order_relaxed);
    atomic_store_explicit(&s->caller, caller, memory_order_relaxed);
    atomic_store_explicit(&s->kind, kind, memory_order_release);
}
int pvz2_stall_native_wait(unsigned kind, uintptr_t object, uintptr_t caller) {
    if (!pvz2_logging_enabled) return -1;
    int at = native_slot(1);
    if (at < 0) return -1;
    WaitSlot *s = &slots[at];
    atomic_store_explicit(&s->native_object, object, memory_order_relaxed);
    atomic_store_explicit(&s->native_caller, caller, memory_order_relaxed);
    atomic_store_explicit(&s->native_kind, kind, memory_order_release);
    return at;
}
void pvz2_stall_native_done(int at) {
    /* A live thread owns its slot until exit; another thread cannot reclaim it
     * while the wrapped call is executing. -1 means the table was unavailable. */
    if ((unsigned)at < WATCH_SLOTS)
        atomic_store_explicit(&slots[at].native_kind, PVZ2_NATIVE_NONE, memory_order_release);
}
int pvz2_stall_sync(unsigned kind, uintptr_t condition, uintptr_t mutex, uintptr_t caller) {
    if (!pvz2_logging_enabled) return -1;
    int at = native_slot(1);
    if (at < 0) return -1;
    SyncSlot *s = &sync_slots[at];
    atomic_store_explicit(&s->condition, condition, memory_order_relaxed);
    atomic_store_explicit(&s->mutex, mutex, memory_order_relaxed);
    atomic_store_explicit(&s->caller, caller, memory_order_relaxed);
    atomic_store_explicit(&s->kind, kind, memory_order_release);
    return at;
}
void pvz2_stall_sync_done(int at) {
    if ((unsigned)at < WATCH_SLOTS)
        atomic_store_explicit(&sync_slots[at].kind, PVZ2_SYNC_NONE, memory_order_release);
}
void pvz2_stall_wait_done(void) {
    if (!pvz2_logging_enabled) return;
    if (own_slot >= 0) atomic_store(&slots[own_slot].kind, PVZ2_WAIT_NONE);
}
void pvz2_stall_thread_exit(void) {
    if (!pvz2_logging_enabled) return;
    if (own_slot < 0) return;
    atomic_store(&slots[own_slot].kind, PVZ2_WAIT_NONE);
    atomic_store(&slots[own_slot].native_kind, PVZ2_NATIVE_NONE);
    atomic_store(&sync_slots[own_slot].kind, PVZ2_SYNC_NONE);
    atomic_store(&slots[own_slot].tid, 0);
    own_slot = -1;
}
static const char *phase_names[] = {"input", "pump", "draw", "callbacks",
    "keyboard", "present", "report"};
static const char *wait_names[] = {"none", "condition", "timed-condition",
    "join", "condition-destroy", "semaphore", "fsync", "rename"};
static const char *native_names[] = {"none", "semaphore", "mutex", "gpu-finish", "read", "pread"};
static const char *sync_names[] = {"none", "condition-wait", "condition-timedwait",
    "condition-signal", "condition-broadcast"};

/* Separate bounded file: loader.log's writer itself could be the blocked call. */
static void write_line(const char *line) {
    const char *path = DATA_PATH "stall.log";
    SceUID fd = -1;
    bounded_log_write(&fd, path, line, sceClibStrnlen(line, 512));
    if (fd >= 0) sceIoClose(fd);
    /* Keep the independent file even if the regular logger is blocked. Most
     * reports contain loader.log alone, so mirror when its lock is available. */
    telemetry_try_line(line);
}
static void report_thread(int tid, unsigned kind, unsigned object, unsigned caller,
                          unsigned native, unsigned native_object, unsigned native_caller) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    if (tid <= 0 || sceKernelGetThreadInfo(tid, &info) < 0) return;
    char line[384];
    sceClibSnprintf(line, sizeof(line),
        "[THREAD] id=0x%x name=%s status=0x%x kernel_wait=%u:0x%x ticks=%llu mask=0x%x bridge=%s object=0x%x caller=0x%x native=%s object=0x%x caller=0x%x\n",
        tid, info.name, info.status, info.waitType, info.waitId,
        (unsigned long long)info.runClocks, info.currentCpuAffinityMask,
        kind < sizeof(wait_names)/sizeof(wait_names[0]) ? wait_names[kind] : "unknown",
        object, caller, native < sizeof(native_names)/sizeof(native_names[0]) ? native_names[native] : "unknown",
        native_object, native_caller);
    write_line(line);
}
static int watch_main(unsigned args, void *argp) {
    unsigned previous = atomic_load(&main_frame), unchanged = 0, reports = 0, ticks = 0;
    for (;;) {
        sceKernelDelayThread(1000000);
        telemetry_reports_drain();
        ++ticks;
        unsigned frame = atomic_load_explicit(&main_frame, memory_order_acquire);
        int sample = 0;
        if (frame != previous) {
            if (reports) write_line("[RESUMED] frame loop advanced\n");
            previous = frame; unchanged = reports = 0;
            /* A flower can animate forever while a resource worker is stuck.
             * Capture a bounded baseline even when rendering still advances;
             * call it a sample, never assert that an idle worker is deadlocked. */
            if (ticks % 30) continue;
            sample = 1;
        } else {
            ++unchanged;
            /* Three snapshots per hard stall: 5s, 15s, 30s, then stay quiet. */
            if (unchanged != 5 && unchanged != 15 && unchanged != 30) continue;
        }
        unsigned phase = atomic_load(&main_phase);
        char line[160];
        sceClibSnprintf(line, sizeof(line), "[%s] frame=%u unchanged=%us phase=%s\n",
            sample ? "SAMPLE" : "STALL", frame, unchanged,
            phase < sizeof(phase_names)/sizeof(phase_names[0]) ? phase_names[phase] : "unknown");
        write_line(line);
        report_thread(main_tid, 0, 0, 0, 0, 0, 0);
        for (unsigned i = 0; i < WATCH_SLOTS; ++i) {
            WaitSlot *s = &slots[i];
            int tid = atomic_load(&s->tid);
            unsigned kind = atomic_load_explicit(&s->kind, memory_order_acquire);
            if (tid > 0) report_thread(tid, kind, atomic_load(&s->object), atomic_load(&s->caller),
                atomic_load_explicit(&s->native_kind, memory_order_acquire),
                atomic_load(&s->native_object), atomic_load(&s->native_caller));
            SyncSlot *sync = &sync_slots[i];
            unsigned operation = atomic_load_explicit(&sync->kind, memory_order_acquire);
            if (tid > 0 && operation) {
                char context[192];
                sceClibSnprintf(context, sizeof(context),
                    "[SYNC] id=0x%x operation=%s condition=0x%x mutex=0x%x caller=0x%x\n",
                    tid, operation < sizeof(sync_names)/sizeof(sync_names[0]) ? sync_names[operation] : "unknown",
                    atomic_load(&sync->condition), atomic_load(&sync->mutex), atomic_load(&sync->caller));
                write_line(context);
            }
        }
        if (!sample) ++reports;
    }
    return 0;
}
void pvz2_stall_start(void) {
    if (!pvz2_logging_enabled) return;
    main_tid = sceKernelGetThreadId();
    write_line("[BOOT] transition observer, 452-v1.1-rc25\n");
    telemetry_log("SYMBOLS", "stall_start=0x%x", (unsigned)(uintptr_t)&pvz2_stall_start);
    SceUID tid = sceKernelCreateThread("pvz2_stall_watch", watch_main, 160, 16384, 0, 0x70000, NULL);
    int rc = tid < 0 ? tid : sceKernelStartThread(tid, 0, NULL);
    if (rc < 0) {
        if (tid >= 0) sceKernelDeleteThread(tid);
        telemetry_log("STALL_WATCH", "unavailable rc=0x%x", (unsigned)rc);
    } else {
        telemetry_reports_enable();
        telemetry_log("STALL_WATCH", "active; async frame reports; native waits, 30s moving-frame samples, 5/15/30s freezes; mirrored to loader.log; file=" DATA_PATH "stall.log");
    }
}
