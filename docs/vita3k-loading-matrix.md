# Vita3K loading investigation

These are emulator observations, not a claim that the user's Vita loading
problem is fixed. The canonical VPK is now RC8, containing the separately
reproduced [configuration race fix](config-store-race.md) and
[JNI Unicode/allocation fixes](jni-unicode.md), plus
[reduced kernel-wait diagnostic overhead](wait-observer-cost.md). No measured Vita
performance gain is claimed from these experiments.

## Reproducible setup

- Emulator: existing Vita3K v0.2.1 4058-6063154f, copied into
  `out/vita3k-lab/runtime`. Each run manifest records its executable hash.
- Original emulator profile and user saves were not modified. Each run gets
  independent firmware, game data, application files and a copied starting save.
- Game: user-owned 4.5.2 ROW library and versionCode 147 OBB, hashed in each manifest.
- Starting save: the pre-existing emulator save at the title screen leading to
  the player's house, Day 4. This is not the user's current Vita save.
- `scripts/vita3k-lab.py` prepares one named run after the emulator is stopped.
  It records configuration and hashes in `out/vita3k-lab/runs/NAME/run.json`.
- `scripts/stop-vita3k-lab.ps1` stops only the recorded lab process and captures
  its emulator log. Game logs remain in each profile's
  `ux0/data/pvz2/userdata/` directory.
- Default comparison settings: Vulkan, CPU optimization on, surface sync
  disabled, Cubeb audio, 960x544, v-sync, no GDB stub. RC5's initial exploratory
  run used the GDB stub and is not a matched performance baseline.

## Observed results

| Run | Changed condition | Observed outcome |
| --- | --- | --- |
| RC5 initial | Current build, debugger enabled | Title to Day 4, two restarts and exit to title succeeded. Sunflower placement, sun collection and zombie/mower simulation observed. No level completion. |
| RC4 baseline | Build from the user's loader report | Title to Day 4 succeeded. |
| RC4 scheduling stress | All host emulator threads restricted to one logical processor after first load | Level restart succeeded. This is scheduling stress, not emulation of Vita CPU speed or affinity. |
| RC4 surface sync | Enable graphics surface synchronization | Title to Day 4 succeeded. |
| RC4 CPU unoptimized | Disable Dynarmic optimizations and fast memory access | Startup paused over five seconds and recovered; title to Day 4 succeeded. |
| RC4 slow I/O | Add 10 ms to each `sceIoOpen` | Title to Day 4 succeeded. This does not throttle `sceIoRead`/`sceIoPread` bandwidth. |
| RC4 SDL audio | SDL backend, volume 25 | Host Vita3K crashed while opening BGM audio at 32000 Hz. Windows access violation reading address zero. Not a Vita crash or endless-loading reproduction. |
| RC4 combined stress | CPU optimization off, surface sync on, 10 ms opens, Cubeb volume 25 | Title to Day 4, exit to title and re-entry to Day 4 succeeded. Startup watchdog reported a pause followed by recovery. |
| RC5 combined stress | Same combined settings | Startup pause recovered; title to Day 4 succeeded. Paused for capture, then emulator stopped. |
| RC5 read stress | Separate diagnostic build, 2048 KiB/s proportional delay plus 1000 microseconds for each positive native read/pread | Startup and Day 4 succeeded. Largest observed game frame was 2,565,121 microseconds. No endless flower. This is per-reader delay, not a global bandwidth cap. |
| RC6 configuration fix | CPU optimization off, surface sync on, 10 ms opens, Cubeb volume 25 | Day 4 entry, music-setting write, exit to title, re-entry, restart with live resources, defeat screen and Retry to fresh gameplay succeeded. Full process relaunch reached title; saved music volume was confirmed in both file and settings UI. Captured and stopped. |
| RC6 gameplay attempt | Default optimized CPU, Cubeb volume 25 | Day 4 and sunflower placement succeeded. Automated collection missed moving/expired sun; stopped without victory. No pacing defect established. |
| RC7 Unicode/allocation fix | Combined slow CPU/surface sync/10 ms opens/Cubeb volume 25 | Title/profile menus, IME open/cancel, Day 4 entry and exit to title succeeded. Bulk text injection was inconclusive; physical key input displayed. Captured and stopped. |
| RC7 diagnostic gameplay replay | Default CPU/graphics, Cubeb volume 25; bounded native touch replay on cloned save | Day 4 sun collection, planting, coin tutorial and final wave progressed into Ancient Egypt Day 1. Egypt gameplay and exit to title succeeded. Captured and stopped. No hardware stall reproduced. |
| RC7 Egypt combined stress | Progressed save from the preceding run; CPU optimization off, surface sync on, 10 ms opens, 2048 KiB/s per-reader delay plus 1000 microseconds per positive read, Cubeb volume 25 | Cold start recovered after a five-second startup watchdog report. Title, Ancient Egypt Day 1, introduction, planting, sun collection, zombie/mower simulation and restart with live resources succeeded. No Egypt victory or endless flower. Captured and stopped. |
| RC8 wait-slot optimization before label change | Verified progressed save, CPU optimization off, surface sync on, 10 ms opens, Cubeb 25 | Matched title timing did not establish a meaningful scene speedup. Egypt loading, dialogue, planting, pause and exit to title passed. Exact run `wait-token-d` captured and stopped. |
| RC8 canonical release | Exact regular VPK, verified assets, default optimized CPU/graphics/storage, Cubeb 25 | Cold boot, title and saved Ancient Egypt Day 1 introduction succeeded. Captured and stopped. No replay or read-stress code enabled. |

## What this establishes

RC4 itself can pass these tutorial loading paths, so successful RC5 emulator
loads do not establish that its audio Clear fix resolves the reported hardware
stall. Slow execution can produce a watchdog report while work eventually
finishes. Do not treat that report as proof of deadlock.

The emulator reports zero native thread run clocks and zero graphics free-pool
statistics in these runs. Those fields cannot validate Vita scheduling or GPU
memory pressure. Whole-run FPS samples mix menus, loading, pause screens and
gameplay and must not be presented as a matched performance benchmark.

A limited title-screen comparison under the same combined stress selected only
samples before the first touch, with zero texture uploads and zero pixel jobs.
Median game work was 6690 microseconds for RC4 (five samples) and 6565 for RC5
(ten samples). This small difference is within plausible run-to-run variation;
it is not evidence of a gameplay or hardware speedup. The raw samples are in
`out/vita3k-lab/runs/title-timing-comparison.json`.

The RSP client incorrectly discarded `OK` acknowledgements as console output.
It now skips only `O` followed by hex-encoded bytes; a focused regression replay
passed. The initial debugger connection was closed during `vCont`, after which
reconnection timed out. Future sampling must keep the connection alive and
interrupt/detach properly rather than assuming reconnection works.

The diagnostic read delays are opt-in CMake definitions. The build script requires
separate build/output directories for them and passes explicit zero defaults for
normal builds. The compiled ARM regression checks delay against actual returned
bytes, no sleep on EOF/error, and bounded sleep chunks. RC6 release has both
definitions disabled; the guard rejecting stress in the release directory passed.

Next investigation: exercise the user's actual later-game save and additional
worker timing/resource-pressure conditions. Emulator speed and memory statistics
cannot substitute for Vita hardware evidence. Current VPK/source snapshots are
under `out/builds/v1.1-rc7`; previous archives are preserved.

## Bounded gameplay fixture

`-ReplayInput` builds a separate diagnostic VPK. It accepts a one-shot, balanced
touch sequence at `userdata/input-replay.txt`, capped at 4096 events and 60 seconds.
It dispatches through the normal native touch queue, at most one event per frame,
and releases a held pointer on timeout. It does not change currency, difficulty,
game time, saves, or native gameplay state. The release-directory guard rejects
this switch without explicit isolated output/build directories.

`scripts/prepare-day4-replay.py` uses the observed 960x544 Day 4 board. Parts 1
and 2 delivered 2178 and 2600 events; the coin tutorial was handled interactively;
part 3 delivered 2600 events and the next observation showed Ancient Egypt Day 1.
The transition's intermediate reward/cutscene screens were not individually
observed. The diagnostic's touch coordinates must be adapted after any layout or
seed-packet-order change. Do not issue another sequence without checking state.

The initial Windows-generated CRLF header was rejected without dispatching input.
The generator now explicitly writes LF. The production parser regression covers
timing, one event per frame, consume-once behavior, malformed requests and timeout
release (`scripts/check-input-replay.py`). Run records and exact diagnostic hashes
are under `out/vita3k-lab/runs/rc7-input-replay`. Canonical RC7 remains SHA256
`435bce4f3403fab48aa0a0e8b73f158f0ffb395aa93b76f7d7b5745657368cf2`.

`scripts/vita3k-lab.py --save-source PATH` can seed another run from a stopped,
preserved lab userdata directory. It copies only recognized save/config data and
records each source file's hash. This permits later tutorial tests without
modifying the user's original emulator profile or substituting an invented save.

The helper now rejects game-library or archive copies whose SHA-256 differs from
the pinned 4.5.2 inputs. A one-byte archive mismatch in `wait-token-b` was detected
by the comparison guard and that run is excluded. See
[wait observer measurements](wait-observer-cost.md) for the matched repeat and
the preserved mismatch evidence. Its cause and relevance to hardware are unknown.
