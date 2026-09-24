# RC13 private candidate — graphics cleanup and loading

RC13 is a local test candidate, not a published release. The user requested
that changes remain off GitHub until tested on hardware. RC12 remains the
published latest release. No data upload, device installation, save edit or
Vita3K run was performed.

## Confirmed defect

The exact RC12 VitaGL binary starts its garbage collector through a request
semaphore with four credits. A successful request does not mean the collector
has finished. The collector changes `frame_purge_idx` and resets the shared
`frame_elem_purge_idx` while the render thread can retire another buffer.

`check-graphics-gc-arm.py` executes the native `glBufferData` and
`garbage_collector` instructions with controlled scheduling. Pausing the producer
after its list-index load lets the collector advance/reset the cursors. The next
retired allocation lands behind an empty slot. The collector stops at that
empty slot and never frees the allocation. The reproduced result is a graphics
allocation leak, not proof of the supplied boss crash's fault location.

The new signal wrapper waits for all completion credits after a GC request,
then restores them. This keeps the existing worker and four-frame purge age,
but prevents the next producer from racing the cursor reset. A collector error
or five-second nonresponse stops the process before returning to unsafe resource
reuse. It does not attempt a graphics dialog using an unresponsive collector.
All unrelated semaphores, mutexes, condition variables and timeouts retain their
normal behavior.

The pinned allocator also sleeps exactly one second after requesting collection.
Now that the request waits for actual completion, that specific sleep is skipped.
The wrapper recognizes only the call inside the pinned 0xc8-byte native recovery
function; the compiled ARM check verifies that exact path. Other sleeps, including
one-second sleeps elsewhere, remain intact. Allocation recovery still performs
the native GPU synchronization, allocation attempts and maximum retry count.

## Other fixes

- Successful reduced-size RGBA uploads now establish a non-mip filter. Their
  later mip uploads are intentionally suppressed, so leaving a mip-dependent
  filter could make the texture incomplete. Both proactive and OOM-retry paths
  are covered by a regression that fails on RC12.
- If an error dialog cannot initialize, fatal handling exits instead of waiting
  forever for a nonexistent dialog to finish. The normal dialog still waits
  for dismissal. The initialization-failure regression fails on RC12.

## Evidence and limits

The tester's flower distortion and missing map-island pieces were reported on
RC11. The photo establishes missing map art with intact HUD/level markers; it
does not identify which texture or draw failed. The collector and texture changes
need a hardware replay of those symptoms. No claim that the whole boss fight,
all maps, or every loading transition is fixed is justified yet.

The two repeatedly submitted dumps still match older RC6 timezone failures.
The newer boss log is RC11 and lacks a current exception or crash PC. Compiled
checks of the earlier Zomboss placement overwrite and timezone crash still pass.

## GitHub issues reviewed

- [#1: loading flower freeze](https://github.com/LeZergan/pvz2vita/issues/1):
  downloaded its attached log. It is RC6, ends at frame 300, has three pixel
  workers and no observer snapshot of the freeze. Previously fixed worker
  notification/condition paths and the new graphics cleanup regression are
  relevant, but the issue remains open pending a new hardware run.
- [#2: Reflourished request](https://github.com/LeZergan/pvz2vita/issues/2): a
  request to port a different game/mod build, outside this stability repair.
  No linked game data was downloaded.
- [#3: high pause-menu CPU](https://github.com/LeZergan/pvz2vita/issues/3): RC11,
  reported CPU percentages only; no log or measured FPS. The supplied separate
  logs reach about 60 FPS under light load. This does not establish an uncapped
  pause loop or distinguish game work, GPU waits and worker activity. Still
  unresolved; the one-second allocator delay fix is not a claim to solve this
  battery/CPU issue. No comments, closures or publication were made.

## Hardware check

Install the private RC13 VPK over the existing loader; retain all game data and
saves. Repeat menu/map/level transitions in one session, inspect the flower and
map islands, retry the reported Zomboss fight, and open/close the pause menu.
Preserve `userdata/loader.log`, `userdata/stall.log` and any newly generated dump
before launching again. The log must identify `452-v1.1-rc13`.
