# Plants vs. Zombies 2 for PS Vita — v1.0

**Build: `452-v1-rc6`.**

Unofficial Vita port of Android 4.5.2 ROW, with touchscreen controls, Vita keyboard
entry and local saves.

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

## Known issues

- Slowdowns during power activation.
- FPS drops when many zombies are on screen.
- You may experience crashes; the loader is very early in development.
- First-time shader compilation can stall new scenes.

The latest texture changes still need a hardware retest. Full-world completion
and long sessions remain under test. See [testing status](private-testing.md).

## Credits

The port-specific implementation and documentation are **fully vibecoded** through
OpenAI Codex, directed and tested by LeZergan. Original game and community code
retain their authorship. Thanks to PopCap / EA, TheFloW, Rinnegatamante,
Volodymyr Atamanenko, GrapheneCt, VitaSDK and the other contributors listed in
[Credits](../README.md#credits) and [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).
