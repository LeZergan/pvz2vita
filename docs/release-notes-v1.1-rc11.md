# Plants vs. Zombies 2 for PS Vita — 1.1 RC11 beta

This beta includes the loading and runtime fixes developed since the previous
GitHub source update, plus targeted shader and logging performance changes.

## Changes

- Replaced the texture-worker condition handoff with per-worker notifications
  and atomic completion. Missing notifications cannot strand a completed job;
  buffers remain owned until all workers finish. Parallel conversion stays on.
- Reuses matching compiled vertex/fragment pairs during the current session.
  Each game program still gets normal linking and its own attribute bindings.
  The cache is bounded and falls back to normal compilation when unavailable.
- Moves periodic frame-log writes off the renderer. Slow storage drops queued
  reports rather than blocking gameplay; shader hits and dropped reports are
  visible in the log.
- Fixes an audio Clear/callback lock cycle, concurrent configuration startup,
  and JNI Unicode character/byte handling and reference cleanup.
- Reduces wait-diagnostic overhead and records native wait context for loading
  freezes. Retains the graphics cleanup, import, stack, time and storage fixes.

## Updating

Install `pvz2-vita-latest.vpk` over the existing app using VitaShell. Keep your
current `libPVZ2.so`, `game.obb` and `userdata/`. No game data is included in
this release and no data replacement is required.

Build ID: `452-v1.1-rc11`. VPK size: **1,137,128 bytes**.

SHA-256:
```text
e27a648868a47c531d164c2af96a36d8f3bcc7b73cf79354138658de63ea5d29
```

## Validation and remaining issues

Production-code tests cover shader ownership and fallback, 500,000 concurrent
log reports, pixel conversion and lost notifications, audio locking,
configuration and JNI behavior. Compiled ARM checks exercise the shipped
VitaGL cache/link/attribute path and game program cleanup with GPU calls mocked.
The VPK's archive, executable, metadata, resource index and artwork were checked.

The latest RC10 hardware log reaches 15,300 frames without a stall report.
It shows approximately 595–599 ms of linking in two hitch windows, plus separate
busy-gameplay drops around 40 FPS and longer transition spikes. RC11 targets
repeat shader compilation and synchronous reporting. **RC11 hardware FPS gains
are not yet measured; this is not a locked 60 FPS or complete-playthrough claim.**
First-time shader compilation still occurs. No Vita3K was used for RC11.

For a problem, retain `ux0:data/pvz2/userdata/loader.log` and `stall.log` before
relaunching, and include the world/level/action and whether the flower animated.
For a hard freeze, let the observer collect up to 30 seconds of snapshots.
See [the performance analysis](rc11-performance.md) for detailed evidence.
