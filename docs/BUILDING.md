# Building the Vita target

## Dependencies

- A softfp VitaSDK toolchain. The current build uses arm-vita-eabi GCC 10.3.0.
  Hard-float libraries must not be mixed with the game's base AAPCS ABI.
- CMake, Ninja, Python 3.12 and Pillow (`py -3.12 -m pip install Pillow`).
- SDK libraries: vitaGL, vitaShaRK, SceShaccCgExt, math-neon, pthread,
  OpenSLES and taiHEN, plus the usual VitaSDK system stubs. The small kubridge
  import stub is included; the kernel plugin itself is not.
- The current local vitaGL source is based on commit
  `bbbeab84bc029a273b3f27d11c5afd9f1dc4184d`. The build expects the normal
  `vglInitExtended` resolution-fallback return convention and the exported
  `gxm_context` and `gxm_color_surfaces_addr` symbols. See
  [DEPENDENCIES.json](DEPENDENCIES.json) for hashes of the libraries used in the current build.
  Those hashes record the tested binaries; they do not establish reproducible
  rebuild flags for every externally installed dependency.

## Supply the matching archive locally

Place your archive at `game/game.obb`, or pass `-GameObb` below. The index
generator validates the full OBB SHA-256 before writing the small bundled index.
The game library is required on the Vita, but is not needed to compile the port.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `libPVZ2.so` | 18198492 | `eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00` |
| `game.obb` | 656855040 | `aa76069dd3f5120cdf733de5e9b5946eab53d8bfdaccb4cb06bdd92bc0aa3abe` |

## Windows build

```powershell
./scripts/build-vita.ps1 -SoftfpVitaSdk C:/tools/vitasdk -GameObb D:/game-files/game.obb
```

The script prepares LiveArea PNGs, generates the resource index, configures the
active CMake target and copies the resulting VPK to `out/pvz2-vita-latest.vpk`.
LiveArea preparation reads the supplied XML and supports the original `a1`
gate and the new `ad0` custom launch frame. It generates an explicit CMake file
list, so old images left in the build directory are not included in the VPK.
Missing artwork or a missing launch link fails preparation; configuring without
the generated file list fails instead of creating a VPK without the artwork.
The original workstation's softfp SDK is the local default when present;
otherwise the script uses `VITASDK`. The explicit parameter overrides both.
The script rejects hard-float SDK defaults. No emulator is started.

On a host with the same softfp dependencies already installed, the underlying
steps are:

```sh
python3 scripts/prepare-livearea.py
python3 scripts/build-rsb-index.py --obb /path/to/game.obb
cmake -S vita/direct -B build-vita-direct -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-vita-direct
```

Keep title ID `PVZ2VITA1` and `ATTRIBUTE2=12`: the extended memory budget is
required. Data is stored under `ux0:data/pvz2/`, with saves and other generated
files under `userdata/`. Game source/asset files are never compiled into the VPK.

## Small source checks

With a host GCC installed and `out/` created:

```sh
python3 scripts/check-touch-render.py
python3 scripts/check-program-cache.py
python3 scripts/check-program-binding.py
python3 scripts/check-placement.py
python3 scripts/check-worker-affinity.py
python3 scripts/check-pixel-workers.py
python3 scripts/check-texture-units.py
python3 scripts/check-time-bridge.py
python3 scripts/check-filesystem-bridge.py
python3 scripts/check-deadlines.py
python3 scripts/check-stall-watch.py
python3 scripts/check-thread-bridge.py
python3 scripts/check-thread-attributes.py
python3 scripts/check-import-resolver.py
```

These compile the production functions with minimal system adapters. They
check multi-contact IDs, read failures, idle sampling recovery, IME release
barriers, projection upload caching, program lifetime, texture unit metadata,
parallel pixel conversion, worker fallbacks and affinity verification. They use no graphics,
audio, emulator or desktop control. The other `check-*.py` scripts have
additional local SDK/game-data prerequisites documented in their source.

For the RC4 placement crash regression, an additional bounded instruction check
uses the exact locally supplied game library and the built unstripped Vita ELF:

```sh
python3 -m pip install pyelftools unicorn
python3 scripts/check-placement-arm.py --game-lib /path/to/libPVZ2.so --loader-elf build-vita-direct/pvz2_loader
```

It reproduces the original fallback and invalid delete argument, then verifies
the compiled ARM/Thumb bridge, production marker and fingerprint rejection.
It runs only the isolated routines; it does not boot the game or Vita3K.
See [the crash analysis](zomboss-crash.md). Neither this check nor CI
replaces a physical Vita replay of the Zomboss encounter.

The RC6 timezone crash has a separate bounded ARM regression:

```sh
python3 scripts/check-time-arm.py --game-lib /path/to/libPVZ2.so --loader-elf build-vita-direct/pvz2_loader
```

Optionally pass `--old-loader-elf /path/to/rc6-loader.elf` to reproduce the
original NULL dereference and storage-buffer overwrite before checking the
fixes. The supplied game library stays local. See [the time/ABI crash analysis](time-crash.md).

Hardware checks remain necessary: leave a menu idle, resume touch, overlap two
fingers, play a busy wave, save and relaunch. Return `userdata/loader.log`.
Touch statistics and `mat4_skipped` are included in the bounded frame reports.
See [the private testing guide](private-testing.md) for the current device
baseline and the short tester checklist.

The intermittent transition investigation has an isolated compiled SDK check:

```sh
python3 scripts/check-cond-destroy-arm.py --loader-elf build-vita-direct/pvz2_loader
```

Pass `--old-loader-elf /path/to/rc1-loader.elf` to reproduce the original leaked
condition-list locks first. No game files are needed for this check.
[Diagnosis and current device evidence](level-transition-stall.md).

The RC3 import audit uses the actual linked import tables and executes the new
math functions with their ARM calling convention:

```sh
python3 scripts/check-imports-arm.py --game-lib /path/to/libPVZ2.so --loader-elf build-vita-direct/pvz2_loader
```

Pass `--old-loader-elf /path/to/rc2-loader.elf` to reproduce its missing imports
and incorrect double return. The supplied library stays local. See
[the audit and limitations](runtime-import-audit.md).

RC4 also checks graphics program cleanup against the native library's actual
program table and the original game's cleanup branches:

```sh
python3 scripts/check-program-lifetime-arm.py --game-lib /path/to/libPVZ2.so --loader-elf build-vita-direct/pvz2_loader
```

Optionally pass `--old-loader-elf /path/to/rc3-loader.elf` to reproduce invalid
deletion first. This verifies the pinned SDK's 1024-program table limit used by
the guard. Run it when replacing the vitaGL dependency; a different native table
layout must be assessed before changing that limit. [Cleanup analysis](program-cleanup.md).
