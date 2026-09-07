# RC6: CPU texture work and loading textures

## Changes

Large RGBA reductions and alpha-to-RGBA conversions use persistent workers.
The caller and workers receive disjoint output row ranges and read immutable
input. The caller waits for all rows before uploading or freeing either buffer.
Workers sleep on condition variables when idle. There is one active shared job;
another caller uses the inline path rather than blocking behind it. Small images
stay inline. Failed worker creation leaves a partial pool or the inline path.

On normal Vita hardware the caller remains on core 0 and the two pixel workers
request cores 1 and 2 individually, with the existing verified shared-mask fallback.
An already-unlocked fourth core adds a third worker. Startup verifies the requested
mask before exposing that core to the game. No marker file is needed.

Alpha atlas reduction now averages source alpha directly into RGBA output.
For a 1024×1024 alpha upload reduced to 512×512, temporary conversion memory
falls from 5 MiB (4 MiB expansion plus 1 MiB reduction) to 1 MiB. Output bytes
match the previous expansion followed by a 2×2 integer box average.

Texture metadata previously followed the last bind on any unit. Switching from
an alpha atlas on unit 1 back to an existing RGBA binding on unit 0 could apply
unit 1's alpha/reduction/failure state to unit 0's upload. The wrapper now updates
the active binding on unit changes and reconciles upload-time state with the
driver, including raw calls made by loader-owned rendering. Deletion clears
metadata before texture IDs can be reused.

This is a verified bridge bug and a plausible contributor to the reported white
loading-flower flicker. A device replay is needed to establish that the reported
visual symptom is gone. The driver's allocation path already zeroes null-data
texture storage; adding another clear would not address this tracking issue.

Allocation failures during converted subimages are reported with bounded texture
diagnostics and do not submit an incompatible pointer or full-sized region to
half-sized storage. Very thin alpha textures keep a valid nonzero allocation.

## Validation and limits

`check-pixel-workers.py` compiles the production pool and checks byte equality,
odd sizes, unaligned inputs, transparent alpha, concurrent callers, buffer lifetime,
and zero/one/two/three-worker operation. `check-texture-units.py` exercises the
production binding and upload functions against a mocked driver, including alpha
and RGBA units, raw bindings, failed allocations, ID reuse and fused subimages.
`check-worker-affinity.py` checks verified normal/unlocked masks and fallbacks.

These are small CPU-only checks. They do not start the game, a GPU context, Vita3K
or desktop control. The Vita VPK is built with the project's softfp SDK. No RC6
hardware FPS, loading-time or flower-flicker result is claimed.

The original native game update remains sequential. Parallel texture work helps
only when those conversions occur; worker creation does not split game logic,
shader compilation or GL submission automatically. The new bounded `[PIXELS]`
report counts completed work alongside the existing kernel thread counters.

## Upstream references reviewed

- [VitaSDK thread API](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/threadmgr/thread.h): affinity changes and kernel thread information.
- [VitaSDK CPU masks](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/cpu.h): ordinary application cores and the system core.
- [vitaGL](https://github.com/Rinnegatamante/vitaGL): rendering and thread-safety checks; graphics calls stay on the rendering thread.
- [CapUnlocker](https://github.com/GrapheneCt/CapUnlocker): optional access to the fourth core for game applications.
