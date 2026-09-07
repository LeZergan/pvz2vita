# Plants vs. Zombies 2 for PS Vita — v1

**Prepared release text. Current artifact: RC6, private testing. No release is published.**

Take the lawn through time on PS Vita. This unofficial native port supports
the Android 4.5.2 ROW game, with touchscreen controls, Vita keyboard entry and
local progress stored on the console.

## Included

- Simple setup: `libPVZ2.so` and `game.obb` in `ux0:data/pvz2/`.
- Saves, settings, caches and logs together in the automatically created `userdata/`.
- Clear boot messages for missing or incompatible files, missing dependencies
  and an unwritable save folder; migration preserves older saves.
- Resource indexing, persistent caches, reduced redundant graphics work and
  parallel preparation of large textures across available application cores.
- One-pass alpha atlas conversion with lower temporary memory use.
- Correct active-unit texture tracking during uploads and the placement bounds
  fix for the reported Ancient Egypt Zomboss crash.
- The supplied LiveArea design, with standard republic / KingTorro credits.
- Bounded logs for reporting problems: 2 MiB plus one previous copy per port log.

## Installation and updates

Follow [Setup](../README.md#setup). Existing testers can install the new VPK over
their current build and keep their data and saves. Game files and system plugins
are not supplied by the source repository.

## Current limits

60 FPS is the target; demanding scenes and first-time shader compilation still
drop below it. The native game update/render path remains limited by its main
thread. Previous builds have reached gameplay on real hardware, but full-world
completion, long sessions and RC6's new texture changes still need testing.
See [private testing](private-testing.md) for the exact evidence and replay route.

## Credits

The port-specific implementation and documentation are **fully vibecoded** through
OpenAI Codex, directed and tested by LeZergan. Original game and community code
retain their authorship. Thanks to PopCap / EA, TheFloW, Rinnegatamante,
Volodymyr Atamanenko, GrapheneCt, VitaSDK and the other contributors listed in
[Credits](../README.md#credits) and [THIRD_PARTY.md](../THIRD_PARTY.md).
