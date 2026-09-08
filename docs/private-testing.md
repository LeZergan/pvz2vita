# 1.1 RC2 device check

Install the new VPK over v1.0 and keep the game files and `userdata/`.
Back up `userdata/` before testing. No data conversion or fresh save is required.

## Current evidence

The September 9 RC1 device run confirms the UTC-4 conversion fix and reaches
8,400 frames, then stops reporting after a level transition. The supplied log
does not identify the blocked call. RC2 fixes a reproduced SDK condition-lock
leak, condition attributes and Android pthread errors, and adds bounded stall
snapshots. See [the transition diagnosis](level-transition-stall.md).

RC2 passes isolated ARM and source checks; its level transitions still need
physical Vita confirmation.

## Device route

1. Launch with your normal console timezone. Confirm the log build is
   `452-v1.1-rc2`; the new `[TIME]` line records the actual offset and a successful
   epoch conversion into the 44-byte Android structure.
2. Load the existing profile, finish Ancient Egypt level 2 and return to the map.
   Play several levels consecutively without restarting; include another level
   that previously stalled.
3. Leave a menu idle, resume touch, and suspend/resume the Vita.
4. Play Ancient Egypt Zomboss and a busy wave when available.
5. Finish a level, close/reopen the game and verify saved progress.

Keep `userdata/loader.log` before relaunching after a failure; include `userdata/stall.log`, its `.previous` copy if present, and the matching
Vita core dump. Each port log is capped at 2 MiB plus one previous copy. Do not
attach proprietary game files or private saves to a public source issue.

## Worker counters

`[PIXELS] worker=... mask=...` records startup affinity. Positive `worker_px` in
a frame report confirms completed helper work; zero is normal when no texture
conversion was needed. `[THREADS]` reports kernel runtime and affinity samples.
Compare FPS on the same save and scene; the ABI checks do not measure Vita FPS.
