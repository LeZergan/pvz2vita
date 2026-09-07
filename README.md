> [!CAUTION]
> ## WARNING: THIS PORT WAS WRITTEN WITH AI
>
> **The port-specific code and this documentation were generated with OpenAI Codex.
> Fully vibecoded, directed and tested by LeZergan.**
>
> Inherited community code and the original game retain their own authorship and
> licenses. The port has not had a complete playthrough. Expect unexpected bugs.

# Plants vs. Zombies 2 — PS Vita

Unofficial PS Vita loader for the Android 4.5.2 ROW build of *Plants vs. Zombies 2*.

[Setup](#setup) — [Report a problem](../../issues/new?template=bug-report.yml)

| | |
| :-- | :-- |
| Loader | PvZ2 v1 RC6 (`452-v1-rc6`) |
| Supported Android set | 4.5.2 ROW, version 147, ARMv7 |
| Data path | `ux0:data/pvz2` |
| Licence | [MIT](LICENSE), with third-party notices |

No Android game files are included. You must supply your own matching
`libPVZ2.so` and main OBB.

## Current release status

RC6 is a private test candidate. No public download is available yet.

- **Install `pvz2-vita-latest.vpk` over the previous build.** Keep your game files
  and saves. No data rebuild is needed.
- **The latest changes still need a Vita replay.** RC6 adds parallel texture
  preparation and fixes texture tracking during loading. Earlier builds reached
  gameplay on hardware; RC6's loading-flower flicker and performance changes
  have passed source checks but need a device comparison.

Changes are listed in [the v1 notes](docs/release-notes-v1.md).

## Requirements

Install the following on a homebrew-enabled Vita before the loader:

| Component | Location | Notes |
| :-- | :-- | :-- |
| [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases) | — | Used to install the VPK and copy files |
| [kubridge](https://github.com/TheOfficialFloW/kubridge) | `*KERNEL` | Required; reboot after installing |
| `libshacccg.suprx` | `ur0:data/` or `ur0:data/external/` | [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) can install it |

Use the **4.5.2 ROW / version 147** game files. Other versions and regional builds
are not interchangeable. [Supported file sizes and hashes](docs/BUILDING.md#supply-the-matching-archive-locally)
are documented. Boot errors explain missing files, dependencies and save-folder problems.

## Setup

### 1. Install the loader

Install `pvz2-vita-latest.vpk` with VitaShell.

### 2. Prepare the data folder

Put your `libPVZ2.so` and main OBB in a folder named `pvz2`. Rename the OBB
to `game.obb`. A prepared tester pack already contains this folder.

### 3. Copy the data to the Vita

Copy the `pvz2` folder into `ux0:data/`. The result must contain:

```text
ux0:data/pvz2/
├── libPVZ2.so
├── game.obb
└── userdata/       ← created automatically
```

Launch **Plants vs Zombies 2** from LiveArea. Do not nest a second `pvz2` folder
inside the first. The older `main.147.com.ea.game.pvz2_row.obb` name is also accepted.

### Updating later

Install the new VPK over the old version. Keep both game files and `userdata/`.
Back up `userdata/` to preserve your progress.

## Sending a log

1. Play until the problem happens.
2. Close the game from LiveArea if it is still running.
3. Do not launch it again yet. `loader.log` is reset at the start of each launch.
4. Copy `ux0:data/pvz2/userdata/loader.log` off the Vita using VitaShell.
5. Attach it to a [bug report](../../issues/new?template=bug-report.yml).

Send the whole file. Include `runtime.log`, `jni.log` and a previous log copy if
available. Each port log is capped at **2 MiB plus one previous copy**.

For a crash, preserve the matching `psp2core-…-eboot.bin.psp2dmp` too. Core dumps
are separate system files and can contain game state; send them through the
private testing channel.

## Reporting problems

Please report freezes, input problems, lost progress, broken graphics, audio
stutter and severe performance drops as well as crashes.

Use the [bug report form](../../issues/new?template=bug-report.yml). Include:

- the build ID and exact world, level or menu
- what happened, and what you expected instead
- whether it happens every time and how long the game had been running
- your Vita model, firmware and any performance plugins or clock changes

Never upload the proprietary game library, APKs, OBBs or saves containing private information.

## Controls and saves

Use the touchscreen to navigate and play. **Circle** goes back, **Start** opens
the menu and **Square** deletes text. Tap a text field to open the Vita keyboard;
confirm replaces the field, cancel keeps it. The game's character rules still apply.

Saves, settings, logs and caches live in `ux0:data/pvz2/userdata/`. Older save
locations migrate automatically; conflicting copies are preserved and reported.

## Optional performance plugins

[CapUnlocker](https://github.com/GrapheneCt/CapUnlocker) exposes the reserved fourth
core. RC6 detects it automatically if installed. The loader runs without it and
uses the three normal application cores. No extra marker file is required.

## Known limitations

- First-time shader compilation can stall new scenes.
- Demanding menus and crowded waves can fall below the 60 FPS target.
- The native game update/render path remains limited by its main thread, even
  while workers prepare textures on other cores.
- Full-world completion and long sessions still need broader hardware coverage.

See [the private testing guide](docs/private-testing.md) for the current evidence
and the short test route.

## Building from source

The loader requires the softfp VitaSDK and the libraries listed in
[the build guide](docs/BUILDING.md). A hard-float SDK is rejected.

```powershell
.\scripts\build-vita.ps1 -SoftfpVitaSdk C:/tools/vitasdk -GameObb D:/game-files/game.obb
```

The output is `out/pvz2-vita-latest.vpk`. VPKs, extracted game data and proprietary
Android files are excluded from source control.

## Legal

No Android executable, library, OBB or gameplay archive is included or linked.
This is an unofficial fan project, unaffiliated with PopCap Games or Electronic
Arts. Game names, artwork and trademarks belong to their respective owners.

## Credits

- LeZergan — project direction and real Vita testing; OpenAI Codex — AI-generated port work
- standard republic — LiveArea assets; KingTorro — port credit in the supplied layout
- Andy "TheFloW" Nguyen — `.so` loader groundwork, so_util, fios and kubridge
- Rinnegatamante — vitaGL, vitaShaRK and math-neon
- Volodymyr Atamanenko — soloader-boilerplate and FalsoJNI
- GrapheneCt — shared Vita runtime groundwork
- OptiJuegos / PvZ2Native — 4.5.2 lifecycle and input behavior reference
- Brad Conte — SHA-1; Michael G Schwern — time conversion; the miniz authors and VitaSDK team
- PopCap Games and Electronic Arts — game and IP; not affiliated

Full attributions are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Licence

Loader source is licensed under the [MIT License](LICENSE). Inherited and
third-party components retain their own notices. Game data and branded artwork
are not relicensed by the port's source license.
