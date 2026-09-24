# RC21: fixed 30 FPS and opt-in diagnostics

Build `452-v1.1-rc21` prepares the v1 release candidate. The SFO remains `01.01`
for compatibility with existing installations. This build has no new console
or Vita3K validation and has not been published.

## Changes

- Fixed 30 FPS from initialization. Removed selectable frame rates, preference
  reads/writes, setter, unbounded/vsync override and the misleading package name.
  Existing preference files are ignored and left intact. The scheduler itself
  now accepts only the fixed cadence. Vblank presentation and sleeping fractional
  deadlines remain; no spinning, extra cores, clock ceiling or texture memory.
- Preserve the tester-approved pointer. The existing L+R, Down+Cross chord opens
  a single-button controls guide. It still requires pausing the level first.
- Logging is off unless the user manually creates `ux0:data/pvz2/logging/` and
  relaunches. Setup and packaging never create this directory. A regular file
  named `logging` does not enable it. The startup directory check occurs once,
  after save migration and before worker startup. A manually supplied directory
  is preserved by migration. Logs retain their existing `userdata/` locations.
- The switch covers loader, runtime and JNI logging, observer startup, native
  wait instrumentation, periodic heap/thread/audio/resource statistics and
  draw/upload timing. Disabled loggers return before formatting, locks or I/O;
  old logs are not truncated. On-screen error handling remains available.
- Correct the diagnostic slow-frame threshold from 18 ms to 35 ms. A normal
  33.3 ms capped frame must not be counted as a missed frame.

## Tester log reviewed

`D:/Мой диск/pvz2vita/loader.log`, modified September 15, is an RC20 recording.
It reaches frame 6,300 with 21 reported windows. The last windows are approximately
29.99–30.00 FPS; earlier windows include 19.30 FPS. The longest frame is
2,984,587 us. There are no explicit FATAL/ERROR/STALL records; the final audio
queue, asset error and touch error counters are zero. JNI references settle at
15. These are observations of this recording, not proof of long-play stability.
The log cannot isolate every long frame's cause. Diagnostic overhead removal is
verified in code/PC tests, but its effect on these Vita hitches is unmeasured.

## Verification

- All 52 local suites pass, including supplied OBB/cache handling and compiled
  ARM wrappers, pixels, allocation guards, time, synchronization and lifetimes.
- The JNI host harness runs 100 rounds in one process: 40 million threaded
  transient allocations plus direct-buffer/object-array/20,000-live-reference
  cases each round. Every round returns to zero live references and recycles
  all 128 tracking slots. This is an accelerated component soak, not gameplay.
- 18,144,000 simulated frames cover seven days of fractional 30 Hz deadlines,
  variable work and scheduler jitter, 32-bit microsecond wrap and 10,000 long
  stall recoveries. CPU headroom reduction, immediate boost and cooldown pass.
- One million disabled runtime/JNI/reset calls per test process make no file,
  mutex or console calls. Enabled output and mutex-creation failure also pass.
  Missing directory, regular file, directory opt-in and preserved migration are
  checked. The asynchronous report queue still passes concurrent/wrap tests.
- Actual ARM semaphore-wrapper measurement with mocked kernel: logging off is
  45 instructions with zero thread-ID calls or diagnostic memory barriers;
  logging on is 83–395 instructions across slots 0–39, with one ID query.
  Kernel execution time is excluded; these are not FPS/temperature measurements.
- Package checks cover SELF, SFO, art, resource index, matching build output,
  disabled stress/replay features and absence of frame-rate overrides and a
  packaged logging directory. Symbols, source and evidence are archived beside
  the VPK for matching future crash reports.

No hardware temperature, scene-wide minimum FPS or crash-free duration is
certified by these PC tests. The earlier console-wide corruption report still
has no confirmed root cause in the available evidence.
