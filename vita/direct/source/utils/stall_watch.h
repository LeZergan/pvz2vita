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
#define PVZ2_WAIT(kind, object) pvz2_stall_wait(kind, (uintptr_t)(object), \
    (uintptr_t)__builtin_return_address(0))
#endif
