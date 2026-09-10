"""Check the diagnostic replay parser and bounded delivery using production C."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix='input-replay-', dir=root/'out'))
source = (root/'vita/direct/source/utils/input_replay.c').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include "'))
prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#define PVZ2_INPUT_REPLAY 1
#define DATA_PATH ""
#define PVZ2_INPUT_TOUCH_DOWN 0
#define PVZ2_INPUT_TOUCH_UP 3
static int delivered, held, rejected;
void pvz2_input_push_touch(int id, int phase, int x, int y) {
    assert(id == 255 && x >= 0 && x < 960 && y >= 0 && y < 544);
    assert(phase == (held ? 3 : 0)); held = phase == 0; ++delivered;
}
void telemetry_log(const char *tag, const char *message, ...) {
    if (message[0] == 'r') ++rejected;
}
'''
suffix = r'''
static void request(const char *s) {
    count = cursor = 0; started = next_probe = 0;
    active_touch = delivered = held = rejected = 0;
    FILE *f = fopen(REPLAY_PATH, "wb"); assert(f);
    fputs(s, f); assert(!fclose(f));
}
int main(void) {
    request("PVZ2_INPUT_REPLAY_V1\n0 0 1 2\n20 3 1 2\n");
    pvz2_input_replay_tick(1000000); assert(delivered == 1 && held);
    pvz2_input_replay_tick(1001000); assert(delivered == 1);
    pvz2_input_replay_tick(1020000); assert(delivered == 2 && !held && !count);
    pvz2_input_replay_tick(3000000); assert(delivered == 2); /* consumed once */
    const char *invalid[] = {
        "wrong\n0 0 1 2\n20 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n0 0 1 2\n", /* held touch */
        "PVZ2_INPUT_REPLAY_V1\n0 3 1 2\n", /* up without down */
        "PVZ2_INPUT_REPLAY_V1\n0 0 960 2\n20 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n0 0 -1 2\n20 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n0 0 1 544\n20 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n0 0 1 2 extra\n20 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n20 0 1 2\n10 3 1 2\n",
        "PVZ2_INPUT_REPLAY_V1\n0 0 1 2\n60001 3 1 2\n"
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        request(invalid[i]); pvz2_input_replay_tick(1000000);
        assert(rejected && !delivered && !count);
    }
    request("PVZ2_INPUT_REPLAY_V1\n0 0 1 2\n0 3 1 2\n0 0 1 2\n0 3 1 2\n");
    pvz2_input_replay_tick(1000000); assert(delivered == 1);
    pvz2_input_replay_tick(1000000); assert(delivered == 2);
    pvz2_input_replay_tick(67000000);
    assert(delivered == 4 && !held && !count); /* timeout releases held pointer */
    puts("PASS: valid timing, one event/frame, consume once, malformed requests rejected, timeout releases pointer");
}
'''
(work/'check.c').write_text(prefix + source + suffix)
subprocess.run(['gcc', '-std=gnu11', '-O2', str(work/'check.c'), '-o', str(work/'check.exe')], check=True)
r = subprocess.run([str(work/'check.exe')], cwd=work, capture_output=True, text=True)
(work/'result.txt').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr, end=''); print(work); r.check_returncode()
