# RC25: shared CPU work and allocation-failure recovery

## Supplied crash evidence

These four dumps are new evidence, not the older timezone dumps discussed in
historical notes. Their module segment sizes and abort call offsets match the
preserved builds below. The core does not contain a complete heap snapshot.

| Dump timestamp | Matching build | Failed request | Game caller return |
| --- | --- | ---: | --- |
| 1789106304 | RC11 | 6 MiB | `libPVZ2+0xd9c250` |
| 1789207760 | RC11 | 4.5 MiB | `libPVZ2+0xb4efc4` |
| 1789324252 | RC13 | 4.5 MiB | `libPVZ2+0xb4efc4` |
| 1790019455 | RC24 | 4.5 MiB | `libPVZ2+0xb4efc4` |

All stop in the loader's deliberate abort path (`_kill_r`, signal 6), with
`std::bad_alloc` in the main-thread stack. Saved frames lead through the game's
`operator new` throw at `0xe5f1bc`; its only throwing branch follows a failed
`malloc` and absent new handler. At `0xb4eef0`, the game grows a vector of
36-byte records: saved r6 is `0x20000`, so the new request is `0x480000` bytes,
while the old capacity is `0x240000`. The other caller preserves `0x600000` in
saved r5. These are ordinary large allocations, not a near-4-GiB overflow.
Stack scanning alone is not a full unwind; the request reconstruction uses the
exact supplied library's prologues and instructions.

Dump SHA-256, in the same order:

```text
2df39a8609e35ff62e221d77c470f22f24a9529d75a16235c31617c54b635bda
ef6b34c7c8cd21d6ee9536b3bc31dfdd099151f7c2ea3c42e105094f2f79e6cf
ea3a8360290531a560a3ce8756490b15208ac9d3c530f8735a0181eb72979064
bcabcd125e304ee8aaf6212cc3b42c16910edb99b39ad1c35a5b5946678170e9
```

The evidence establishes allocation failure, but not whether fragmentation,
retained live data or a leak exhausted the fixed 160 MiB heap. It does not
establish free system memory at the instant of each failure.

## Bounded fallback

The Android allocation imports still try newlib first. Only a failed request
between 256 KiB and 8 MiB can obtain a kernel USER_RW block, rounded to 4 KiB.
The fallback has a 16 MiB aggregate cap and 16 slots. It checks reported free
USER memory and declines if the request would leave less than 8 MiB. Other
kernel users can allocate concurrently, so this check is not a system-wide
reservation guarantee. No pages are allocated in advance; the fixed heap,
graphics pools and graphics reserve are unchanged.

All six allocation/free imports share ownership handling, including calloc
zeroing, page alignment, realloc copying, NULL/zero behavior and preservation
of the old pointer on failure. Ordinary heap frees reject the fallback address
range without a lock/table scan. A fallback free releases the kernel block and
its budget. No extra allocation-tracking table is enabled for normal play.
Recovery reports respect the existing manual logging opt-in.

This permits continued execution when the fixed heap fails but enough USER
pages remain. It cannot recover from true system-memory exhaustion or cure an
unbounded game leak. It does not suppress C++ exceptions or skip game updates.

## Three-core scheduling and clocks

Main, Android pthread workers, OpenSL output, texture workers and vitaGL's
collector all allow cores 0–2 (`0x70000`). There is no single-core assignment,
main-core reservation or fourth-core probe. Priorities and requested thread
stacks are preserved. A rejected affinity change is reported when logging is
enabled; no restrictive fallback mask is installed.

Two texture helpers and the caller claim 16-row strips from one atomic queue.
Late helpers own no reserved slice, so available participants drain the work.
The completion barrier still waits for every helper to acknowledge the job
before buffers may be reused. Small and concurrent submissions retain their
serial fallback. ETC block alignment and decoded output are unchanged.

At boot the CPU requests 500 MHz and reads back the accepted clock. If 500 is
unavailable, it requests 444. The frame policy's floor is 444; supported 500 MHz
returns on expensive frames. It does not install or configure an overclock
plugin, change voltages, or change the existing GPU/bus clocks. No guarantee
that stock firmware accepts 500 MHz is made.

The engine's serial update/render thread remains serial; scheduler freedom
cannot turn every game function into parallel work or guarantee 100% load on
each core. Busy-waiting is not used to inflate utilization. Fixed 30 FPS pacing
and vblank presentation remain; no lower-bound FPS claim is made.

## Verification

55 local suites pass, including:

- Real ARM game `operator new` reproduces its exception on a failed allocation.
  The built loader recovers both 4.5/6 MiB requests with mocked spare USER
  memory, returns to the game, and frees via the correct allocation domain.
- Host fault injection covers page/base/info failures, limits, alignment,
  calloc overflow/zeroing, realloc ownership transitions and 600 concurrent
  allocation/growth/free cycles with no retained blocks.
- Texture results match the independent serial reference. A deterministic
  delayed-helper case proves the caller drains every strip before helpers can
  start, without bypassing the completion barrier. Lost notifications,
  concurrent callers, buffer guards and allocation-failure fallback pass.
- Affinity checks verify main/native workers use the shared three-core mask.
- Clock-policy checks cover supported 500/444 and stock 444-only behavior.
- Fixed-cadence checks cover 18,144,000 simulated frames, clock wrap and stalls.

These tests use synthetic allocations and mocked system calls, not a physical
Vita or a game session. Heavy-wave frame times, thermal behavior and recurrence
of the four crashes need a device run of `452-v1.1-rc25`. Preserve fresh dumps
and opt-in logs before relaunching. Original dumps/game files remain local.
