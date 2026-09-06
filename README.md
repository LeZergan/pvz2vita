# PvZ2 Vita

Private development repository for the **Plants vs. Zombies 2 4.5.2 ROW,
version 147** PlayStation Vita compatibility port. No GitHub release has been
published. The Android ARMv7 library runs natively through a Vita runtime bridge.

The game library, OBB, saves, logs, SDK and compiled VPK are not included in this
source repository. Supply your own matching game files. This is the port's
source code, not EA/PopCap's proprietary game source.

## Install an existing test build

Copy a folder containing `libPVZ2.so` and `game.obb` to `ux0:data/pvz2/` and
install the VPK with VitaShell. Existing users install only the new VPK.
The game keeps all generated files in `ux0:data/pvz2/userdata/`; back up that
folder for your saves. The older long version-147 OBB filename is accepted.

The Vita needs kubridge enabled under `*KERNEL` and the shader compiler installed
with ShaRKBR33D. See [the installation guide](vita/direct/INSTALL.txt).

## Development status

RC2 has booted on a physical Vita through frame 17700. The bundled resource
index took 170 ms, down from a 5367 ms full archive scan. Native workers are
observed on cores 1–2. Audio queues and JNI object counts stayed stable in that
run. Busy scenes still fell to approximately 22 FPS; the game call took about
45 ms. Locked 60 FPS and long-session stability are not established.

The current RC3 source adds the supplied LiveArea artwork, separate gesture IDs
for overlapping fingers, touch-read/sampling recovery, an idle keep-awake tick,
and caching of unchanged sprite projection matrices. Host regression checks and
Vita compilation pass. RC3's hardware input and performance results are pending.

## Build and contribute

See [BUILDING.md](BUILDING.md) for dependencies, exact input hashes, build commands
and small noninteractive checks. `vita/direct/` is the active target;
`source/main_452.c` is its entry point. `livearea/sce_sys/` holds the source
artwork. `scripts/prepare-livearea.py` validates and encodes it for packaging.

Keep game files and personal runtime data out of commits. The CI workflow runs
only source checks; it does not build or publish releases. See
[THIRD_PARTY.md](THIRD_PARTY.md) for source origins and notices.
