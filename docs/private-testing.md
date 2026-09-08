# 1.1 RC1 device check

Install the new VPK over v1.0 and keep the game files and `userdata/`.
Back up `userdata/` before testing. No data conversion or fresh save is required.

## Current evidence

The September 8 RC6 report reaches startup and 300 frames, then faults in a
local-time conversion using UTC-4 and timestamp 6. Both texture workers complete
jobs on separate cores. The last reported memory and audio counters do not show
exhaustion or starvation. See [the diagnosis and fixes](time-crash.md).

`452-v1.1-rc1` passes the isolated ARM reproduction and source regression checks.
No physical Vita run of this candidate has been supplied yet.

## Device route

1. Launch with your normal console timezone. Confirm the log build is
   `452-v1.1-rc1`; the new `[TIME]` line records the actual offset and a successful
   epoch conversion into the 44-byte Android structure.
2. Load the existing profile, open the level selector and play a normal level.
3. Leave a menu idle, resume touch, and suspend/resume the Vita.
4. Play Ancient Egypt Zomboss and a busy wave when available.
5. Finish a level, close/reopen the game and verify saved progress.

Keep `userdata/loader.log` before relaunching after a failure; include the matching
Vita core dump. Each port log is capped at 2 MiB plus one previous copy. Do not
attach proprietary game files or private saves to a public source issue.

## Worker counters

`[PIXELS] worker=... mask=...` records startup affinity. Positive `worker_px` in
a frame report confirms completed helper work; zero is normal when no texture
conversion was needed. `[THREADS]` reports kernel runtime and affinity samples.
Compare FPS on the same save and scene; the ABI checks do not measure Vita FPS.
