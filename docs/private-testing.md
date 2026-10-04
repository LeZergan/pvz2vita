# RC29 Vita check

Install RC29 over the previous loader and keep the game files. Back up
`ux0:data/pvz2/userdata/` first. LeZergan confirms RC29 works on Vita; this route
helps report specific settings, saves, mod or gameplay problems consistently.

1. Open startup settings with **Down + Cross (X) + L + R**, then release all
   buttons. Confirm steady display, override Off and English on a fresh or
   migrated RC26/RC27 configuration. Test with headphones and after a system dialog.
2. Cancel a small currency edit; confirm no change. Apply a small edit to the
   intended player with Done, relaunch and check the value in-game. Complete a
   level and relaunch again to confirm normal saving.
3. If using mods, confirm the intended OBB filename shown in settings. Mods
   translating English assets should use override Off. Close the game before
   replacing an archive, and preserve the save backup.
4. Play several levels, revisit Zen Garden and include a busy wave. Check pause,
   suspend/resume, audio, pointer/touch, seed-tray controls and keyboard input.
5. For a problem report, manually create `ux0:data/pvz2/logging/`, relaunch and
   reproduce. Close the game and preserve `userdata/loader.log` before another
   launch. Include applicable `stall.log`, `runtime.log`, `jni.log`, previous
   copies and the matching Vita core dump. Remove the logging directory to
   disable diagnostics again.

Identify the build as `452-v1.1-rc29`, and include world/level, mod name, language
settings, reproduction steps and Vita/plugin setup. Compare performance on the
same scene and save; the 30 FPS target is not a minimum for crowded waves.
Do not attach proprietary game data or private saves to public issues.

See [setup](../README.md#setup), [settings](settings.md),
[RC29 audit](rc29-release-audit.md) and [update scenarios](rc29-update-scenarios.md).

## Historical RC4 device check

Install the new VPK over v1.0 and keep the game files and `userdata/`.
Back up `userdata/` before testing. No data conversion or fresh save is required.

### Evidence available at RC4

The September 9 RC1 device run confirms the UTC-4 conversion fix and reaches
8,400 frames, then stops reporting after a level transition. The supplied log
does not identify the blocked call. RC2 fixes a reproduced SDK condition-lock
leak, condition attributes and Android pthread errors, and adds bounded stall
snapshots. See [the transition diagnosis](level-transition-stall.md).

The newer Zen Garden ZIP is an RC2 run ending after frame 26,400. It contains
only `loader.log`; the separate wait snapshots are missing. Workers were active
on both cores 1/2 and the audio queue showed no starvation before reporting stopped.

RC3 additionally resolves 32 previously missing imports, preserves requested
worker stack sizes, and supplies thread stack/priority query outputs.
[The runtime audit](runtime-import-audit.md) records what was reproduced and
what remains unknown. All 25 local checks pass; this does not establish a fix
for every physical Vita stall.

RC4 fixes a further reproduced graphics cleanup bug: the old program-validity
stub answered true for handle zero, allowing an unchecked driver deletion.
Eight targeted checks pass on RC4, including the compiled game cleanup, native
driver bounds, and earlier ARM regressions. See [graphics cleanup](program-cleanup.md).

### RC4 device route

1. Launch with your normal console timezone. Confirm the log build is
   `452-v1.1-rc4`; `[IMPORTS] unresolved=0` confirms import resolution and the
   `[TIME]` line records the actual offset and a successful
   epoch conversion into the 44-byte Android structure.
2. Load the existing profile, finish Ancient Egypt level 2 and return to the map.
   Play several levels consecutively without restarting; include another level
   that previously stalled.
   Enter and leave Zen Garden several times, including after completing a level.
3. Leave a menu idle, resume touch, and suspend/resume the Vita.
4. Play Ancient Egypt Zomboss and a busy wave when available.
5. Finish a level, close/reopen the game and verify saved progress.

Keep `userdata/loader.log` before relaunching after a failure; include `userdata/stall.log`, its `.previous` copy if present, and the matching
Vita core dump. Each port log is capped at 2 MiB plus one previous copy. Do not
attach proprietary game files or private saves to a public source issue.

### Worker counters

`[PIXELS] worker=... mask=...` records startup affinity. Positive `worker_px` in
a frame report confirms completed helper work; zero is normal when no texture
conversion was needed. `[THREADS]` reports kernel runtime and affinity samples.
Compare FPS on the same save and scene; the ABI checks do not measure Vita FPS.

`[ASSETIO]` shows resource requests and cache extraction progress without taking
the resource lock. If the flower continues animating while loading never ends,
these reports can distinguish progressing extraction from an unchanged resource
state. The separate stall observer reports when completed frames stop instead.
