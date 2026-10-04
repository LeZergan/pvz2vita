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

[Download RC29](https://github.com/LeZergan/pvz2vita/releases/tag/v1.1-rc29) — [Setup](#setup) — [Settings guide](docs/settings.md) — [Report a problem](https://github.com/LeZergan/pvz2vita/issues/new?template=bug-report.yml) — [Discord](https://discord.gg/KgSzU8nd8g)

| | |
| :-- | :-- |
| Current release | PvZ2 1.1 RC29 (`452-v1.1-rc29`) |
| Previous release | [1.1 RC25](https://github.com/LeZergan/pvz2vita/releases/tag/v1.1-rc25) |
| Supported Android set | 4.5.2 ROW, version 147, ARMv7 |
| Data path | `ux0:data/pvz2` |
| Licence | [MIT](LICENSE), with third-party notices |

No Android game files are included. You must supply your own matching
`libPVZ2.so` and main OBB.

## Current release status

**RC29 adds boot settings, language selection, backed-up coins/gems editing,
and support for compatible modded OBBs.** It fixes the shoulder-button boot
chord, uses two display buffers to address settings flashing, and waits for all
launch buttons to be released before accepting changes.

Language override defaults **Off / English**. Mods that translate English assets
use those assets automatically. There are no stock OBB size/hash checks or mod
allowlists; the installed archive supplies its own resource names and offsets.
All optimizations are enabled, including sprite upload suppression and improved
shader metadata caching. There is no optimization switch in settings.

The fixed **30 FPS** target, shared three-core scheduling and accepted 500 MHz
or fallback 444 MHz clock policy are retained. Busy zombie waves can still drop
below the target. No new hardware FPS gain is claimed.

**Version 1.1 is available and works on PS Vita.**

See the [release notes](docs/release-notes-v1.1-rc29.md),
[release audit](docs/rc29-release-audit.md), and
[additional update scenarios](docs/rc29-update-scenarios.md).

## Requirements

Install the following on a homebrew-enabled Vita before the loader:

| Component | Location | Notes |
| :-- | :-- | :-- |
| [VitaShell](https://github.com/TheOfficialFloW/VitaShell/releases) | — | Used to install the VPK and copy files |
| [kubridge](https://github.com/TheOfficialFloW/kubridge) | `*KERNEL` | Required; reboot after installing |
| `libshacccg.suprx` | `ur0:data/` or `ur0:data/external/` | [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) can install it |

Use the **4.5.2 ROW / version 147 ARMv7 library** and an OBB compatible with that
engine. A compatible modded OBB can replace the original archive.
[Library requirements and mod archive support](docs/BUILDING.md#game-library-and-mod-archives).

## Setup

### 1. Install the loader

Download `pvz2-vita-latest.vpk` from the
[RC29 release](https://github.com/LeZergan/pvz2vita/releases/tag/v1.1-rc29).
Copy it to the Vita and install it with VitaShell. Existing users can install
over the previous loader; keep the game files and back up `userdata/` first.

### 2. Prepare the data folder

Obtain the matching ARMv7 `libPVZ2.so` from your own Android 4.5.2 ROW APK
(`lib/armeabi-v7a/libPVZ2.so` inside the APK). An APK can be opened as a ZIP.
Put that library and your main OBB directly inside a folder named `pvz2`.
The original `main.147.com.ea.game.pvz2_row.obb` filename is supported.
Keep the matching game library when using a compatible mod archive.

### 3. Copy the data to the Vita

Copy the `pvz2` folder into `ux0:data/`. The result must contain:

```text
ux0:data/pvz2/
├── libPVZ2.so
├── main.147.com.ea.game.pvz2_row.obb
└── userdata/       ← created automatically
```

Launch **Plants vs Zombies 2** from LiveArea. The game creates `userdata/`
automatically; a new install needs no downloaded save or prebuilt resource index.
The first boot indexes the installed archive and can take longer.

Do not nest a second `pvz2` folder inside the first. `game.obb` is also accepted
and takes priority when both archive filenames exist. Settings show which
archive was selected.

### 4. Open settings when needed

Hold **D-pad Down + Cross (X) + L + R** as the app starts, within the first
1.5 seconds of boot. **Release all four buttons** when settings appear, then
use Up/Down to select a row and Left/Right or Cross to change an option.

| Setting | Behavior |
| :-- | :-- |
| Language override | Off by default; the game requests English |
| Language | Available with override On: English, German, Spanish, French, Italian, Brazilian Portuguese |
| Player | Select the profile whose coins/gems you want to edit |
| Coins / Gems | Cross opens nine-digit entry; Cross accepts the staged value, Circle cancels that value |
| Done | Applies pending changes and exits; relaunch to play |

Outside number entry, **Circle exits and discards pending changes**. The screen
also shows OBB filename, folder, bytes/MiB and readable RSB information.
All optimizations are always enabled; there is no sprite optimization toggle.
Read the [settings and save-backup guide](docs/settings.md) before editing saves.

### Updating later

Install the new VPK over the old version. Keep both game files and `userdata/`.
Back up `userdata/` to preserve your progress.

RC26/RC27 language preferences reset once to **Off / English**. Deliberate choices
saved by RC28/RC29 persist. For a mod that translates English assets, leave
override Off. Optional alternate languages need their assets in the selected OBB.

To change mods, close the game, back up your saves, and replace the selected
archive in `ux0:data/pvz2/` with the intended compatible OBB. Relaunch to index
it. No stock checksum or original archive length is required. Compatibility with
a newer Android engine version is not established by changing the OBB alone.

### Setup troubleshooting

- **Settings do not open:** hold the full chord while the app starts, then release
  it once the menu appears. The [settings guide](docs/settings.md#opening-settings)
  also documents a file-based fallback.
- **Missing plugin/compiler:** enable kubridge under `*KERNEL` and reboot;
  install `libshacccg.suprx` using ShaRKBR33D at a listed path above.
- **Missing library/archive:** check the exact folder layout and file names.
  The ARMv7 library must match 4.5.2 ROW; mod OBBs need no stock size/hash match.
- **No editable player:** launch normally and create a player first. Unsupported
  or damaged saves remain unchanged; language settings still work.
- **Cannot write saves/cache:** check free space and the memory card/SD2Vita.
  If Done reports currencies saved but settings failed, retry Done after correcting
  storage; the currencies have already committed.

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
.\scripts\build-vita.ps1 -Configuration Release -SoftfpVitaSdk C:/tools/vitasdk
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
