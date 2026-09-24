# RC15 — local optimization and failure containment

All work was off-device as requested. No Vita3K, full-game execution, hardware
transfer/install, save changes or external publication occurred. This is a
private candidate. Physical temperature and game-wide FPS were not measured.

## CPU work and memory traffic

ETC1 decode and optional box reduction now execute together on the existing
worker pool. Each helper decodes a 4x4 block into a 64-byte local array and writes
only its final output rows. Partial blocks, odd dimensions and unaligned input
are supported. Workers finish before the shared reusable output is uploaded or
reused. GPU operations stay on the rendering thread.

For a 2048x2048 atlas, RC14 first decoded 16 MiB of RGBA, then allocated 4 MiB
for reduction and read the large image again. RC15 uses a reusable 4 MiB output,
avoiding that full-image intermediate and extra pass. GPU storage stays 4 MiB.
The same reduction now covers compressed NULL storage definitions and full
subimage fills, which previously could create full-size temporary images.

A local Windows benchmark with two helpers compared 40 conversions, alternating
the order in four rounds: RC14 serial decode plus parallel reduction took
502137 microseconds; fused parallel conversion took 204875 microseconds, about
2.45x throughput in that synthetic PC test. Input was a uniform ETC1 atlas.
This is not a device FPS or temperature result. Actual frame gains depend on
whether texture preparation is on that frame's critical path.

## Frame pacing and power

The previous optional frame limiter started each interval from the preceding
wakeup, accumulating scheduler oversleep and truncating fractional periods.
The replacement carries absolute fractional deadlines and discards old schedule
debt after a long frame. It sleeps rather than spins. Normal default presentation
still uses vblank; explicit caps use the new limiter. Disabling vblank no longer
leaves an unbounded rendering loop: software pacing limits it to 60 FPS.

RC14's power policy treated every deliberately capped 30 FPS frame as overload
because it exceeded the fixed 19 ms threshold, repeatedly forcing 444 MHz.
Thresholds now follow the configured frame budget, keeping the same conservative
headroom ratios, immediate load boost and cooldown. This makes the existing
optional 30 FPS cap useful for reducing sustained work without defeating CPU
adaptation. Heavy workloads can still require 444 MHz; no GPU overclock or
unverified GPU clock reduction was added.

## Native draw allocation failure

The actual linked glDrawArrays instructions pass a NULL allocation straight to
sceClibMemcpy after native pool fallback/collection is exhausted. The isolated
ARM regression reproduces this on archived RC14. A linker wrapper retains native
allocation/recovery behavior, then checks NULL returns from six pinned draw
routines and stops the process before their unchecked copy. Successful allocation
arguments/returns and non-draw NULL recovery remain intact.

This contains a reproduced memory-pressure crash; it cannot manufacture memory
or keep that exhausted scene playing. It deliberately avoids rendering a fatal
dialog through an exhausted driver and avoids storage writes in this failure
path. Only the debug-console message is guaranteed before exit code 7. The six
native function sizes are asserted by the compiled test; replacement VitaGL
libraries require revalidation. This is not proof of the tester's Zen Garden
fault location or the console-wide corrupted-file cause.

## Validation

- Independent frozen RC14 serial decoder oracle versus full/fused output,
  including both ETC1 block modes, selector patterns, odd/partial dimensions,
  capacity guards, concurrent callers, notification loss and immediate reuse.
- Eight worker/failure configurations, each with RGBA/alpha/ETC1 cases.
- Texture wrapper checks verify 4 MiB CPU/GPU storage, NULL-definition/full-fill
  agreement, offsets, payload bounds, failure metadata and ID reuse.
- Synthetic ten-minute 30/60 Hz pacing traces with oversleep, fractional time,
  stalls and cap limits; power tests include deliberate 30 FPS waiting.
- Native ARM allocation failure, GC, texture storage, shader lifetime, placement
  and time checks; host audio, worker affinity, reports, observer, filesystem,
  config-store and binding checks.

The per-draw diagnostic syscall hypothesis was rejected: those markers are
already compiled out in the current shipping configuration. No performance
claim is based on removing them. No new hardware test is requested in this work.
