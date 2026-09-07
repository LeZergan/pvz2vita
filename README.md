> [!IMPORTANT]
> **FULLY VIBECODED VITA PORT**
>
> The port-specific implementation and documentation were written with AI coding
> assistants through OpenAI Codex, with **LeZergan** directing development and
> testing on real hardware. This credit applies to our port work; the original
> game and the community libraries below retain their own authorship and licenses.

<p align="center">
  <img src="livearea/sce_sys/icon0.png" width="128" height="128" alt="PvZ2 Vita icon">
</p>

# Plants vs. Zombies 2 — PS Vita

Defend your lawn through time, now on PlayStation Vita.

An unofficial native port of **Plants vs. Zombies 2 4.5.2 ROW (version 147)**,
with touchscreen controls, the Vita keyboard, local saves and a simple two-file
data setup. The original Android ARMv7 game runs through a Vita compatibility
layer, with graphics provided by vitaGL.

[Setup](#setup) · [What's included](#whats-included) ·
[Report a problem](https://github.com/LeZergan/pvz2vita/issues/new/choose) ·
[Build](BUILDING.md) · [Credits](#credits)

| | |
| :-- | :-- |
| Current build | **v1 release candidate 5** · `452-v1-rc5` |
| Supported game | Android **4.5.2 ROW**, version **147**, ARMv7 |
| Game data | `ux0:data/pvz2/` |
| Saves, settings, logs and caches | `ux0:data/pvz2/userdata/` |
| Distribution | Private testing; **no GitHub release published** |

Supply your own matching game files. This repository contains the Vita port's
source, not the proprietary game library, game assets or your save data.

## What's included

- Native touchscreen play, separate gesture tracking for overlapping fingers,
  and recovery when Vita touch sampling stops.
- Vita keyboard entry: confirm replaces the selected field, cancel keeps it.
- Local saves and automatic migration from the older data layout.
- Boot messages that identify missing files, missing runtime dependencies and
  an unwritable save folder, with instructions to fix the problem.
- Bundled resource indexing, persistent resource/shader caches and reduced
  redundant graphics work to shorten loading and reduce frame overhead.
- Worker scheduling across the Vita's available user cores, with separate
  audio scheduling. The game update/render path still has a main-thread limit.
- Custom LiveArea artwork and bounded diagnostic logs.

### New in this candidate

RC5 brings the new **ad0 LiveArea design**, including its custom launch frame,
background, bubble icon and loading artwork. Packaging now follows the supplied
layout's asset references and keeps its launch link and credits intact. The
tester pack verifies every LiveArea file against the built VPK.

The fix for the reported Ancient Egypt Zomboss crash remains. The game's
fallback placement could write beyond its 9×10 scratch grid
and overwrite a pointer later passed to `delete`. The port now clips those grid
writes while preserving native placement and object positioning.

The original failure was reproduced in an isolated ARM instruction check.
The compiled fix preserves the pointer and passes the bridge and bounds checks.
The latest physical RC4 log has no reported crash or resource error through
frame 16800. It does not identify the played level or record a completed boss
encounter. RC5 changes packaging and the build label; gameplay code is unchanged.
See [the crash analysis](docs/zomboss-crash.md) for the evidence and limits.

## Requirements

| Item | What you need |
| :-- | :-- |
| Console | A homebrew-enabled PlayStation Vita |
| Installer | VitaShell |
| Kernel bridge | `kubridge.skprx`, enabled under `*KERNEL` in `ur0:tai/config.txt`; reboot after installing it |
| Shader compiler | Install with ShaRKBR33D; `libshacccg.suprx` must be in `ur0:data/` or `ur0:data/external/` |
| Game files | The matching `libPVZ2.so` and version-147 OBB from your own 4.5.2 ROW copy |

The port checks these requirements on boot. See [BUILDING.md](BUILDING.md#supply-the-matching-archive-locally)
for the exact supported file sizes and hashes. Other Android versions and
regional builds are not interchangeable.

## Setup

1. Copy your two game files into `ux0:data/pvz2/`, naming the OBB `game.obb`.
2. Install the supplied test VPK with VitaShell, then launch **Plants vs Zombies 2**.

```text
ux0:data/pvz2/
├── libPVZ2.so
├── game.obb
└── userdata/       ← created automatically by the game
```

If you received a prepared tester folder, copy its `pvz2` folder directly into
`ux0:data/`. Avoid nesting a second `pvz2` folder inside it. The older filename
`main.147.com.ea.game.pvz2_row.obb` is also accepted.

**Updating an existing install:** install only the new VPK over the previous
one. Keep your data and `userdata/` folder. No fresh save or cache deletion is
needed for RC5.

## Controls and saves

Use the touchscreen to navigate, choose plants and play. Tap a text field to
open the Vita keyboard; confirm to replace its value or cancel to keep it.
The game's own character restrictions still apply. **Circle** goes back,
**Start** opens the menu and **Square** deletes text.

Back up **`ux0:data/pvz2/userdata/`** to preserve your progress. The game keeps
its saves, settings and generated files together there. Older save locations
are migrated automatically; conflicting copies are preserved and reported.

## Performance and testing status

The port targets 60 FPS, but **does not maintain 60 FPS in every scene**.
The latest RC4 device log reached frame 16800, with a median sampled window of
55.95 FPS and many windows around 60 FPS. Its final window fell to 19.89 FPS,
with about 49.6 ms inside the native game call and 4.1 ms sampled draw time.
First-time shader compilation and CPU-heavy game updates can still cause drops.

Real Vita logs confirm native workers running on cores 1–2, alongside the main
game/render thread on core 0. Adding worker threads cannot automatically split
the original game's sequential update and render work across all cores.

Boot, menus and gameplay have been exercised on hardware. Full-world completion,
boss encounter completion and long-session stability remain under test. This is a
release candidate, not a claim that every level has been completed without issues.

## Reporting a problem

For a crash, preserve the logs **before launching the game again**:

- `ux0:data/pvz2/userdata/loader.log` and its previous copy, if present.
- `runtime.log` and `jni.log` from the same folder, if available.
- The matching `psp2core-…-eboot.bin.psp2dmp` if Vita generated a core dump.
- Build ID, world/level, what happened immediately before the failure, and
  whether it happens again from the same save.

Each port log is capped at **2 MiB plus one previous copy**. Core dumps are
separate system files and are not part of that limit. Dumps can contain game
state; send them through the private testing channel. Do not attach your game
library or OBB to an issue.

[Open a report](https://github.com/LeZergan/pvz2vita/issues/new/choose).

## Building from source

See [BUILDING.md](BUILDING.md) for the softfp VitaSDK dependencies, build commands
and isolated regression checks. `vita/direct/` is the active target, with
`source/main_452.c` as its entry point. Source checks run in GitHub Actions;
they do not publish a release or replace hardware testing.

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
