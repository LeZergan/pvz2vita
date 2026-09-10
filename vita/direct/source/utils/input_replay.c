/* Diagnostic integration fixture. Only queues real touch events; no game-state,
 * currency, save, difficulty or time-step edits. Compiled out of release builds. */
#if PVZ2_INPUT_REPLAY
#include "input_replay.h"
#include "../java_runtime.h"
#include "telemetry.h"
#include <stdio.h>
#include <string.h>

#define REPLAY_PATH DATA_PATH "input-replay.txt"
#define REPLAY_LIMIT 4096u
typedef struct { unsigned ms; int x, y, phase; } ReplayEvent;
static ReplayEvent events[REPLAY_LIMIT];
static unsigned count, cursor;
static uint64_t started, next_probe;
static int active_touch, last_x, last_y;

static void release_touch(void) {
    if (active_touch) pvz2_input_push_touch(255, PVZ2_INPUT_TOUCH_UP, last_x, last_y);
    active_touch = 0;
}

void pvz2_input_replay_tick(uint64_t now) {
    if (!count) {
        if (now < next_probe) return;
        next_probe = now + 1000000u;
        FILE *f = fopen(REPLAY_PATH, "rb");
        if (!f) return;
        char line[128];
        int ok = fgets(line, sizeof(line), f) && !strcmp(line, "PVZ2_INPUT_REPLAY_V1\n");
        unsigned n = 0, previous = 0;
        int down = 0;
        while (ok && fgets(line, sizeof(line), f)) {
            ReplayEvent event; char trailing;
            if (n == REPLAY_LIMIT || sscanf(line, "%u %d %d %d %c", &event.ms, &event.phase, &event.x, &event.y, &trailing) != 4 ||
                event.ms > 60000 || event.ms < previous || event.x < 0 || event.x >= 960 || event.y < 0 || event.y >= 544 ||
                (event.phase != PVZ2_INPUT_TOUCH_DOWN && event.phase != PVZ2_INPUT_TOUCH_UP) ||
                (event.phase == PVZ2_INPUT_TOUCH_DOWN) == down) { ok = 0; break; }
            events[n++] = event;
            previous = event.ms;
            down = event.phase == PVZ2_INPUT_TOUCH_DOWN;
        }
        if (ferror(f) || down || !n) ok = 0;
        fclose(f);
        /* Consume only this explicitly named diagnostic request, never saves. */
        if (remove(REPLAY_PATH) != 0) { telemetry_log("REPLAY", "request could not be consumed"); return; }
        if (!ok) { telemetry_log("REPLAY", "rejected invalid/unbalanced/overlong request"); return; }
        count = n; cursor = 0; started = now;
        telemetry_log("REPLAY", "started %u events, duration=%u ms", n, previous);
    }
    /* Do not flood the normal input queue after a slow frame. At most one
     * event per game frame preserves down/up delivery through UI_ProcessEvents. */
    if (cursor < count && now - started >= (uint64_t)events[cursor].ms * 1000u) {
        ReplayEvent *e = &events[cursor++];
        last_x = e->x; last_y = e->y;
        active_touch = e->phase == PVZ2_INPUT_TOUCH_DOWN;
        pvz2_input_push_touch(255, e->phase, e->x, e->y);
    }
    if (cursor == count || now - started > 65000000u) {
        release_touch();
        telemetry_log("REPLAY", "finished delivered=%u/%u elapsed_ms=%u", cursor, count, (unsigned)((now-started)/1000u));
        count = 0;
    }
}
#endif
