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

[Download](https://github.com/LeZergan/pvz2vita/releases/tag/v1.1-rc25) — [Report a problem](https://github.com/LeZergan/pvz2vita/issues/new?template=bug-report.yml) — [Discord](https://discord.gg/KgSzU8nd8g)

| | |
| :-- | :-- |
| Loader | PvZ2 1.1 RC25 (`452-v1.1-rc25`) |
| Supported Android set | 4.5.2 ROW, version 147, ARMv7 |
| Data path | `ux0:data/pvz2` |
| Licence | [MIT](LICENSE), with third-party notices |

No Android game files are included. You must supply your own matching
`libPVZ2.so` and main OBB.

## Current release status

**1.1 RC25 is available.** Install its VPK over the previous version and keep
your existing game files and saves.

RC25 targets the four newly supplied allocation-failure crashes and improves
CPU scheduling. The main thread, game workers, audio and texture helpers may
run on any of the three application cores. Texture work uses a shared queue,
not fixed per-core slices. The CPU requests 500 MHz when the system accepts it,
otherwise 444 MHz; idle reduction never goes below 444 MHz.

The frame target is fixed at **30 FPS**, with no 60 FPS override. This is a cap
and pacing target, not a verified minimum in heavy waves. RC25 passes 55 local
host/ARM checks; its heavy-wave FPS and crash recovery need physical Vita testing.
It also includes the intervening graphics cleanup, loading, audio, pointer and
keyboard fixes. See the [release notes](docs/release-notes-v1.1-rc25.md) and
[crash/scheduling evidence](docs/rc25-cpu-memory.md).

## Requirements

Install the following on a homebrew-enabled Vita before the loader:

| Component | Location | Notes |
| :-- | :-- | :-- |
| [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases) | — | Used to install the VPK and copy files |
| [kubridge](https://github.com/TheOfficialFloW/kubridge) | `*KERNEL` | Required; reboot after installing |
| `libshacccg.suprx` | `ur0:data/` or `ur0:data/external/` | [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) can install it |

Use **4.5.2 ROW / version 147** game files. Other builds are not supported.
[Check file sizes and hashes](docs/BUILDING.md#supply-the-matching-archive-locally).

## Setup

### 1. Install the loader

Install `pvz2-vita-latest.vpk` with VitaShell.

### 2. Prepare the data folder

Put your `libPVZ2.so` and main OBB in a folder named `pvz2`. Rename the OBB
to `game.obb`.

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

Logging is **off by default**. After setup, manually create the empty directory
`ux0:data/pvz2/logging/`, beside the game files, then relaunch. A file with that
name is not enough. The installer does not create the directory.

1. Play until the problem happens.
2. Close the game from LiveArea if it is still running.
3. Preserve the logs before launching again.
4. Copy `ux0:data/pvz2/userdata/loader.log` off the Vita using VitaShell.
5. Attach it to a [bug report](https://github.com/LeZergan/pvz2vita/issues/new?template=bug-report.yml).

Remove the logging directory and relaunch to disable diagnostics again.
Existing logs are left untouched while logging is off.

Send the whole file. Include `stall.log`, `runtime.log`, `jni.log` and a previous log copy if
available. Each port log is capped at **2 MiB plus one previous copy**.

For a crash, keep the matching `psp2core-…-eboot.bin.psp2dmp` for diagnosis.

## Reporting problems

Use the [bug report form](https://github.com/LeZergan/pvz2vita/issues/new?template=bug-report.yml). Include:

- the build ID and exact world, level or menu
- what happened, and what you expected instead
- whether it happens every time and how long the game had been running
- your Vita model, firmware and any performance plugins or clock changes

Never upload the proprietary game library, APKs, OBBs or saves containing private information.

## Controls and saves

Use touch, the left stick or D-pad to move the pointer; the right stick gives
fine movement. **Cross** presses/drags, **Circle** goes back, **Start** opens
the menu. **L/R** focus the standard seed tray. Hold **L+R**, then **Down+Cross**
for the controls guide (pause first during a level).
Tap a text field to open the Vita keyboard;
confirm replaces the field, cancel keeps it. The game's character rules still apply.

Saves, settings, logs and caches live in `ux0:data/pvz2/userdata/`. Older save
locations migrate automatically; conflicting copies are preserved and reported.

## Known issues

- Slowdowns during power activation.
- FPS drops when many zombies are on screen.
- You may experience crashes; the loader is very early in development.
- First-time shader compilation can stall new scenes.

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

## Contributors

- **LeZergan** — project direction and real Vita testing.
- **OpenAI Codex (AI contributor)** — port implementation, fixes and documentation.

## Credits

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
