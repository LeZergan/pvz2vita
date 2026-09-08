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
} WaitSlot;
static WaitSlot slots[WATCH_SLOTS];
static _Thread_local int own_slot = -1;
static atomic_uint main_frame, main_phase;
static int main_tid;

void pvz2_stall_frame(unsigned frame, unsigned phase) {
    atomic_store_explicit(&main_phase, phase, memory_order_relaxed);
    atomic_store_explicit(&main_frame, frame, memory_order_release);
}
void pvz2_stall_wait(unsigned kind, uintptr_t object, uintptr_t caller) {
    if (own_slot < 0) {
        int tid = sceKernelGetThreadId();
        for (unsigned i = 0; i < WATCH_SLOTS; ++i) {
            int empty = 0;
            if (atomic_compare_exchange_strong(&slots[i].tid, &empty, tid)) {
                own_slot = (int)i;
                break;
            }
        }
    }
    if (own_slot < 0) return;
    WaitSlot *s = &slots[own_slot];
    atomic_store_explicit(&s->object, object, memory_order_relaxed);
    atomic_store_explicit(&s->caller, caller, memory_order_relaxed);
    atomic_store_explicit(&s->kind, kind, memory_order_release);
}
void pvz2_stall_wait_done(void) {
    if (own_slot >= 0) atomic_store(&slots[own_slot].kind, PVZ2_WAIT_NONE);
}
void pvz2_stall_thread_exit(void) {
    if (own_slot < 0) return;
    atomic_store(&slots[own_slot].kind, PVZ2_WAIT_NONE);
    atomic_store(&slots[own_slot].tid, 0);
    own_slot = -1;
}
static const char *phase_names[] = {"input", "pump", "draw", "callbacks",
    "keyboard", "present", "report"};
static const char *wait_names[] = {"none", "condition", "timed-condition",
    "join", "condition-destroy", "semaphore", "fsync", "rename"};

/* Separate bounded file: loader.log's writer itself could be the blocked call. */
static void write_line(const char *line) {
    const char *path = DATA_PATH "stall.log";
    SceUID fd = -1;
    bounded_log_write(&fd, path, line, sceClibStrnlen(line, 512));
    if (fd >= 0) sceIoClose(fd);
}
static void report_thread(int tid, unsigned kind, unsigned object, unsigned caller) {
    SceKernelThreadInfo info = {.size = sizeof(info)};
    if (tid <= 0 || sceKernelGetThreadInfo(tid, &info) < 0) return;
    char line[384];
    sceClibSnprintf(line, sizeof(line),
        "[THREAD] id=0x%x name=%s status=0x%x kernel_wait=%u:0x%x ticks=%llu mask=0x%x bridge=%s object=0x%x caller=0x%x\n",
        tid, info.name, info.status, info.waitType, info.waitId,
        (unsigned long long)info.runClocks, info.currentCpuAffinityMask,
        kind < sizeof(wait_names)/sizeof(wait_names[0]) ? wait_names[kind] : "unknown",
        object, caller);
    write_line(line);
}
static int watch_main(unsigned args, void *argp) {
    unsigned previous = atomic_load(&main_frame), unchanged = 0, reports = 0;
    for (;;) {
        sceKernelDelayThread(1000000);
        unsigned frame = atomic_load_explicit(&main_frame, memory_order_acquire);
        if (frame != previous) {
            if (reports) write_line("[RESUMED] frame loop advanced\n");
            previous = frame; unchanged = reports = 0;
            continue;
        }
        ++unchanged;
        /* Three snapshots per stall: 5s, 15s, 30s, then stay quiet. */
        if (unchanged != 5 && unchanged != 15 && unchanged != 30) continue;
        unsigned phase = atomic_load(&main_phase);
        char line[160];
        sceClibSnprintf(line, sizeof(line), "[STALL] frame=%u unchanged=%us phase=%s\n",
            frame, unchanged, phase < sizeof(phase_names)/sizeof(phase_names[0]) ? phase_names[phase] : "unknown");
        write_line(line);
        report_thread(main_tid, 0, 0, 0);
        for (unsigned i = 0; i < WATCH_SLOTS; ++i) {
            WaitSlot *s = &slots[i];
            int tid = atomic_load(&s->tid);
            unsigned kind = atomic_load_explicit(&s->kind, memory_order_acquire);
            if (tid > 0) report_thread(tid, kind, atomic_load(&s->object), atomic_load(&s->caller));
        }
        ++reports;
    }
    return 0;
}
void pvz2_stall_start(void) {
    main_tid = sceKernelGetThreadId();
    write_line("[BOOT] transition observer, 452-v1.1-rc4\n");
    SceUID tid = sceKernelCreateThread("pvz2_stall_watch", watch_main, 160, 16384, 0, 0x60000, NULL);
    int rc = tid < 0 ? tid : sceKernelStartThread(tid, 0, NULL);
    if (rc < 0) {
        if (tid >= 0) sceKernelDeleteThread(tid);
        telemetry_log("STALL_WATCH", "unavailable rc=0x%x", (unsigned)rc);
    } else telemetry_log("STALL_WATCH", "active; snapshots after 5/15/30s without frames; file=" DATA_PATH "stall.log");
}
