#ifndef PVZ2_POWER_POLICY_H
#define PVZ2_POWER_POLICY_H
#include <stdint.h>

/* Only lower CPU frequency after sustained spare frame time. One expensive
 * frame restores the accepted performance ceiling. GPU clocks are unchanged. */
typedef struct {
    uint64_t next_reduce;
    unsigned light_frames;
    unsigned missed_frames;
    int mhz;
    int ceiling_mhz;
    int floor_mhz;
} Pvz2PowerPolicy;

static inline int pvz2_power_step_budget(Pvz2PowerPolicy *p, uint64_t now,
                                 uint64_t work_us, uint64_t frame_us, unsigned budget_us) {
    if (budget_us < 16667) budget_us = 16667;
    if (budget_us > 1000000) budget_us = 1000000;
    const int ceiling = p->ceiling_mhz == 500 ? 500 : 444;
    const int floor = p->floor_mhz == 444 ? 444 : 222;
    if ((p->mhz != 222 && p->mhz != 333 && p->mhz != 444 && p->mhz != 500) ||
        p->mhz > ceiling || p->mhz < floor) p->mhz = ceiling;
    if (frame_us > (uint64_t)budget_us * 19000 / 16667) {
        if (p->missed_frames < 3) ++p->missed_frames;
    } else p->missed_frames = 0;
    if (work_us > (uint64_t)budget_us * 12000 / 16667 ||
        frame_us > (uint64_t)budget_us * 2 || p->missed_frames >= 3 ||
        (p->mhz == 222 && budget_us < 33333)) {
        p->mhz = ceiling;
        p->light_frames = 0;
        p->next_reduce = now + UINT64_C(10000000);
    } else if (p->mhz > floor && (p->mhz >= 444 || budget_us >= 33333)) {
        uint64_t threshold = p->mhz >= 444 ? (uint64_t)budget_us * 8500 / 16667 : 10000;
        if (!p->missed_frames && work_us < threshold) ++p->light_frames;
        else p->light_frames = 0;
        if (p->light_frames >= 180 && now >= p->next_reduce) {
            p->mhz = p->mhz == 500 ? 444 : (p->mhz == 444 ? 333 : 222);
            p->light_frames = 0;
            p->next_reduce = now + UINT64_C(10000000);
        }
    }
    return p->mhz;
}
static inline int pvz2_power_step(Pvz2PowerPolicy *p, uint64_t now,
                                 uint64_t work_us, uint64_t frame_us) {
    return pvz2_power_step_budget(p, now, work_us, frame_us, 33334);
}
#endif
