# RC14 private hardware test candidate

The supplied `loader (4).log` identifies RC13, Sep 12 2026 13:34:50.
It contains 704 frame windows through frame 211200, with 25 below 30 FPS.
The lowest sustained busy window is about 19.1 FPS; one window includes
a 20.46-second maximum frame. Three 2048x2048 uploads report GL_OUT_OF_MEMORY
late in the session. Workers are already active on cores 1 and 2 with no
reported misplaced workers. The log contains no hard-stall snapshot or fatal
exception and ends with frames advancing at 52.87 FPS. This does not capture
the reported Zen Garden freeze or identify the cause of console-wide storage
errors. The supplied photographs establish missing art, not its texture format.

## Reproduced defects and changes

- Audio STOP joined the output thread. A callback waiting for a lock held by
  the stopping caller deadlocked both threads. The regression reproduces this
  on archived RC13. STOP now waits only for in-flight borrowed PCM, closes the
  port and leaves the consumer parked. Restart reuses it; destruction still
  joins before freeing the object. Callback/context registration and snapshots
  are synchronized, with callbacks invoked outside the bridge lock.
- Decoded ETC1 images bypassed normal texture reduction and allocation-error
  handling. A 2048x2048 atlas now follows the existing parallel RGBA reduction
  path: 1024x1024 RGBA storage (4 MiB versus 16 MiB). This is an allocation
  calculation verified by the upload fixture, not a device FPS measurement.
- ETC1 subimage uploads at the origin redefined the whole atlas; nonzero-offset
  updates were ignored. They now decode and update the existing image through
  the matching subimage path, retaining dimensions and reduction metadata.
  Payload size is checked before decoding. Non-ETC1 subimage behavior is unchanged.
- Compressed allocation fallback now uses the checked placeholder helper and
  establishes failure metadata. Placeholders still omit artwork; this is not
  a claim that every texture can be recovered. Error reports now distinguish
  compressed/image/subimage operations and include format and update context.
- CPU policy lowers 444 to 333 MHz after at least 180 consecutive light frames
  and a ten-second cooldown. One expensive frame restores 444; heavy scenes
  remain at 444. GPU/bus clocks and the 60 FPS target remain unchanged. Clock
  failures disable adaptation. `userdata/cpu_fixed.txt` provides a comparison.
  This does not read a temperature sensor or establish a measured heat reduction.

## Verification

Audio STOP and ETC1 regressions fail on archived RC13 and pass on RC14.
Production policy trace tests cover cooldown, heavy/mixed scenes, immediate
boost and long uptime. Audio, texture, worker affinity, asynchronous reports,
graphics cleanup, stall observer, filesystem and config-store checks pass.
Bounded compiled ARM checks cover texture storage, native graphics cleanup,
shader ownership, timezone and placement. Pixel workers pass eight configurations
including dropped notifications, concurrent callers and buffer lifetime.
These isolated checks do not boot the game or replace physical Vita testing.

## Hardware test required

Install the private candidate VPK over the loader and keep existing data/saves.
Verify BUILD=452-v1.1-rc14, visit Zen Garden and both pictured maps, switch scenes
repeatedly, pause/resume, and play a busy wave. Compare the POWER and PERF lines
with the fixed-clock setting if adaptive clocks introduce visible pacing changes.
Preserve loader.log, runtime.log, stall.log, their .previous files and a fresh
coredump before relaunch. Record the exact console error code and whether it
persists after reboot. The supplied log cannot distinguish a storage/plugin/
system failure from the game's reported freeze.

No Vita3K run, device install, save edit, FTP transfer, Drive upload or GitHub
publication was performed. RC14 remains a private candidate pending hardware
validation; do not publish it as a proven fix for all freezes or slowdowns.
