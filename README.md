> [!CAUTION]
> ## FULLY VIBECODED — VITA PORT WORK WRITTEN WITH AI
>
> **The port-specific implementation and documentation were written with AI
> coding assistants through OpenAI Codex**, with **LeZergan** directing development
> and testing on real hardware. The original game and community libraries retain
> their own authorship and licenses. This project has not had a complete playthrough.

<p align="center">
  <img src="livearea/sce_sys/icon0.png" width="128" height="128" alt="PvZ2 Vita icon">
</p>

# Plants vs. Zombies 2 — PS Vita

Defend your lawn through time, now on PlayStation Vita.

Unofficial native Vita port of the Android **4.5.2 ROW / version 147** game,
with touchscreen play, Vita keyboard entry and local saves.

[Setup](#setup) — [Report a problem](../../issues/new/choose) —
[v1 release notes](docs/release-notes-v1.md) — [Build](BUILDING.md) — [Credits](#credits)

| | |
| :-- | :-- |
| Loader | **v1 release candidate 6** · `452-v1-rc6` |
| Supported Android set | **4.5.2 ROW**, version **147**, ARMv7 |
| Game files | `ux0:data/pvz2/` |
| Saves, settings, logs and caches | `ux0:data/pvz2/userdata/` |
| Availability | **Private testing; no GitHub release published** |
| Source license | [MIT](vita/direct/LICENSE), with retained third-party notices |

No Android game library, game archive or save data is included in the source
repository. Supply your own matching game files.

## Current release status

RC6 is prepared for private testing. Boot, menus and gameplay have been exercised
on Vita in earlier builds. This candidate adds parallel texture preparation and
fixes a texture-unit tracking bug that could apply another texture's alpha,
size or failed-allocation flags during loading.

- Large texture conversions now share actual pixel work between the caller and
  dedicated workers on cores 1–2. An already-unlocked fourth core is detected
  automatically. Alpha conversion and reduction use one pass and a smaller buffer.
- The new LiveArea design, touch recovery, keyboard integration, save migration
  and Ancient Egypt Zomboss placement crash fix are retained.
- **60 FPS is the target; demanding menus, busy waves and first-time shader
  compilation can still run below it.** The native game update/render path
  remains sequential. Full-world completion and long sessions remain under test.

The new CPU and texture checks pass and the Vita VPK builds. RC6's loading-flower
appearance and performance improvement still need a physical Vita comparison.
See [private testing](docs/private-testing.md) for the device evidence and test route.

## Requirements

| Component | Location | Notes |
| :-- | :-- | :-- |
| Homebrew-enabled PS Vita | — | Required |
| [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases) | — | Install the VPK and copy the data |
| [kubridge](https://github.com/TheOfficialFloW/kubridge) | `*KERNEL` in `ur0:tai/config.txt` | Reboot after installing |
| `libshacccg.suprx` | `ur0:data/` or `ur0:data/external/` | [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) can install it |
| Matching game files | `ux0:data/pvz2/` | Your own 4.5.2 ROW library and version-147 OBB |

Boot messages identify missing files, runtime dependencies and an unwritable save
folder. [Exact supported sizes and hashes](BUILDING.md#supply-the-matching-archive-locally)
are documented; other versions and regional builds are not interchangeable.

## Setup

1. Copy `libPVZ2.so` and your OBB into `ux0:data/pvz2/`, naming the OBB `game.obb`.
2. Install the supplied `pvz2-vita-latest.vpk` with VitaShell.
3. Launch **Plants vs Zombies 2** from LiveArea.

```text
ux0:data/pvz2/
├── libPVZ2.so
├── game.obb
└── userdata/       ← created automatically
```

For a prepared tester pack, copy its `pvz2` folder directly into `ux0:data/`.
Avoid nesting a second `pvz2` folder. The older name
`main.147.com.ea.game.pvz2_row.obb` is also accepted.

**Updating:** install the new VPK over the old version. Keep both game files and
`userdata/`. No save reset, marker file or cache deletion is required for RC6.

## Controls and saves

Use the touchscreen to navigate and play. Tap a text field for the Vita keyboard;
confirm replaces its value, cancel keeps it. Game character restrictions still
apply. **Circle** goes back, **Start** opens the menu and **Square** deletes text.

Back up **`ux0:data/pvz2/userdata/`** to preserve progress. Saves, settings and
generated files stay together there. Older save locations migrate automatically;
conflicting copies are preserved and reported.

## Sending a log

Preserve logs **before launching again after a failure**. Send
`ux0:data/pvz2/userdata/loader.log` and its previous copy if present. Include
`runtime.log`, `jni.log` and the matching `psp2core-…-eboot.bin.psp2dmp` if available.

Each port log is capped at **2 MiB plus one previous copy**. Vita core dumps are
separate system files. Send dumps through the private testing channel because
they can contain game state.

## Reporting problems

[Open a report](../../issues/new/choose) with the build ID, world/level, the action
that triggered the issue and whether it repeats from the same save. For the
loading flower, mention whether the flash occurs only on first entry or repeats
on every visit. For performance, include the scene and whether zombies were active.
Do not attach the proprietary library or game archive to an issue.

## Performance and CPU cores

The port uses persistent resource/shader caches, reduced redundant graphics work,
bounded texture memory and parallel CPU texture preparation. Workers sleep when
idle. Graphics calls stay on the rendering thread.

Normal hardware provides three application cores. If
[CapUnlocker](https://github.com/GrapheneCt/CapUnlocker) is already installed,
RC6 verifies availability of the fourth core and uses it too; the plugin is optional.
The loader does not install it. A busy main core can remain the limit even while
workers are active. [CPU implementation and validation](docs/cpu-textures-rc6.md)
describe what has been moved and how device logs measure it.

## Building from source

See [BUILDING.md](BUILDING.md) for the softfp VitaSDK, dependency fingerprints and
build commands. `vita/direct/` is the active target. Small source checks run in
GitHub Actions; the workflow does not publish a release or replace Vita testing.

## Credits

- **LiveArea package** — assets credited to **standard republic** and port to
  **KingTorro** in the supplied layout; those credits are preserved on the LiveArea.
- **LeZergan** — project direction, Vita testing and port presentation.
- **OpenAI Codex / AI coding assistants** — port-specific implementation,
  debugging and documentation under LeZergan's direction: fully vibecoded.
- **Andy “TheFloW” Nguyen** — Android `.so` loader groundwork, so_util, fios and kubridge.
- **Rinnegatamante** — vitaGL, vitaShaRK, math-neon and Vita porting groundwork.
- **Volodymyr Atamanenko** — soloader-boilerplate and FalsoJNI.
- **GrapheneCt** and the other contributors named in the retained source notices
  — shared Vita runtime groundwork.
- **OptiJuegos / PvZ2Native** — the exact 4.5.2 lifecycle and input behavior reference.
- **VitaSDK contributors**, the **miniz authors**, **Brad Conte** (SHA-1) and
  **Michael G Schwern** (time conversion) — tools and supporting code.
- **PopCap Games and Electronic Arts** — Plants vs. Zombies 2 and its game assets.

See [THIRD_PARTY.md](THIRD_PARTY.md) for source origins and license notices.
Community contributions retain their authorship; the AI credit does not replace it.

## Legal and licensing

This is an unofficial fan project, unaffiliated with PopCap Games or Electronic
Arts. Game names, artwork and trademarks belong to their respective owners.
No proprietary game library or gameplay archive is distributed in this repository.

The loader's [MIT license](vita/direct/LICENSE) and per-file notices are retained.
Dependencies have their own licenses, including vitaGL's LGPL terms; game data
and branded artwork are not relicensed by the port's source license.
