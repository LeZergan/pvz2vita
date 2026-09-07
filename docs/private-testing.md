# RC6 private testing

Install RC6 over the existing VPK and keep both game files and `userdata/`.
It adds parallel texture preparation, fused alpha conversion and corrected
texture-unit tracking. The RC5
LiveArea and RC4 placement crash fix remain. New changes pass isolated CPU/GL
checks; no RC6 device run has been supplied yet.

## Latest device evidence

The September 7 RC4 loader log is 41,389 bytes and reaches frame 16800,
covering about 356 seconds of sampled frame windows. It records:

| Check | Observed result |
| --- | --- |
| Fatal, resource, or other reported error lines | None in the supplied loader log |
| Audio queue full / empty waits | 0 / 0 throughout |
| JNI references | 15 live throughout |
| Touch reads | No errors; final 138 down / 138 up, no active contact |
| Worker scheduling | Cores 1–2; no misplaced worker samples |
| Resource index | 150 ms |
| FPS | Median window 55.95; 29 of 56 windows at least 55 FPS |
| Final window | 19.89 FPS; native game 49.6 ms, sampled draws 4.1 ms |
| Placement patch | Installed; no clipped-footprint reports in this run |

The user reports that things appear to work. The log does not name the scene,
record boss completion or prove save persistence across relaunches. There is no
new core dump in the supplied folder; the existing dump is the older RC3 crash.

The slow final window follows a resource-heavy transition. The counters identify
time spent in the native game call, but do not identify the specific game
function responsible. The RC6 changes address verified bridge code issues. They do not establish
which native function dominates that final window or a new FPS result.

## Short device checklist

1. Install RC6 and watch the white flower during loading. Enter/leave the level
   selector twice; record any flash, loading delay and FPS on both visits.
2. Play a regular level and Ancient Egypt Zomboss. For any slowdown, record
   the level and whether it happens during loading, animation or a crowded wave.
3. Leave a menu idle and resume touch; try overlapping fingers and keyboard
   confirmation/cancellation.
4. Complete a level, close and reopen the game, and verify saved progress.
5. Send the matching `userdata/loader.log` with the build ID and the result.
   Preserve it before relaunching after a failure; include a new Vita core dump
   if one was generated. Do not send the game library or OBB in a source issue.

Each port log is capped at 2 MiB plus one previous copy. The current log is about
40 KiB. The tester folder and source repository remain private; no public release
is created by this packaging pass.

## Reading the new counters

`[PIXELS] worker=... mask=...` is emitted once per pixel worker at startup.
In each 300-frame report, `jobs`, `parallel`, `worker_px` and `caller_px` count
completed texture conversions and output pixels for that interval. Positive
`worker_px` proves conversion work completed on helpers; a zero during steady
gameplay is normal if no textures needed conversion. `[THREADS]` retains actual
kernel runtime and affinity measurements. The pool can use fewer workers if a
thread could not be created; conversion still completes inline.

Compare the same save, scene and warm-cache route with RC5. Do not interpret
compiler checks or host CPU timings as Vita FPS results.
