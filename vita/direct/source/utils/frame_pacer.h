#ifndef PVZ2_FRAME_PACER_H
#define PVZ2_FRAME_PACER_H
#include <stdint.h>
#include "fps_preference.h"
typedef struct { uint64_t deadline; unsigned remainder; } Pvz2FramePacer;
/* Absolute deadlines retain fractional microseconds and sleep overshoot.
 * A long frame starts a new schedule, never a burst of catch-up frames. */
static inline unsigned pvz2_frame_delay(Pvz2FramePacer *p, uint64_t now) {
    const unsigned fps = PVZ2_TARGET_FPS;
    unsigned period = 1000000u / fps;
    if (!p->deadline || now > p->deadline + period) {
        p->deadline = now;
        p->remainder = 0;
    }
    unsigned delay = now < p->deadline ? (unsigned)(p->deadline - now) : 0;
    p->deadline += period;
    p->remainder += 1000000u % fps;
    if (p->remainder >= fps) { ++p->deadline; p->remainder -= fps; }
    return delay;
}
#endif
