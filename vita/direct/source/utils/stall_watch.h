#ifndef PVZ2_STALL_WATCH_H
#define PVZ2_STALL_WATCH_H
#include <stdint.h>

enum pvz2_frame_phase {
    PVZ2_FRAME_INPUT, PVZ2_FRAME_PUMP, PVZ2_FRAME_DRAW,
    PVZ2_FRAME_CALLBACKS, PVZ2_FRAME_KEYBOARD, PVZ2_FRAME_PRESENT,
    PVZ2_FRAME_REPORT
};
enum pvz2_wait_kind {
    PVZ2_WAIT_NONE, PVZ2_WAIT_COND, PVZ2_WAIT_TIMED_COND,
    PVZ2_WAIT_JOIN, PVZ2_WAIT_COND_DESTROY, PVZ2_WAIT_SEMA,
    PVZ2_WAIT_FSYNC, PVZ2_WAIT_RENAME
};
void pvz2_stall_start(void);
void pvz2_stall_frame(unsigned frame, unsigned phase);
void pvz2_stall_wait(unsigned kind, uintptr_t object, uintptr_t caller);
void pvz2_stall_wait_done(void);
void pvz2_stall_thread_exit(void);
enum pvz2_native_wait_kind {
    PVZ2_NATIVE_NONE, PVZ2_NATIVE_SEMA, PVZ2_NATIVE_MUTEX,
    PVZ2_NATIVE_GPU, PVZ2_NATIVE_READ, PVZ2_NATIVE_PREAD
};
/* The stack-held slot token avoids another kernel thread-id query/table scan
 * on return. Pair on the same thread; no emulated TLS or allocation is used. */
int pvz2_stall_native_wait(unsigned kind, uintptr_t object, uintptr_t caller);
void pvz2_stall_native_done(int slot);
enum pvz2_sync_kind {
    PVZ2_SYNC_NONE, PVZ2_SYNC_WAIT, PVZ2_SYNC_TIMED_WAIT,
    PVZ2_SYNC_SIGNAL, PVZ2_SYNC_BROADCAST
};
/* Outer SDK condition call, retained while the inner kernel wait changes.
 * Separate from the Android bridge; no TLS or condition dereference. */
int pvz2_stall_sync(unsigned kind, uintptr_t condition, uintptr_t mutex, uintptr_t caller);
void pvz2_stall_sync_done(int slot);
#define PVZ2_WAIT(kind, object) pvz2_stall_wait(kind, (uintptr_t)(object), \
    (uintptr_t)__builtin_return_address(0))
#endif
