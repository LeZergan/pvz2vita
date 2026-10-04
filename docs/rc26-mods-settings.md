# RC26: mod archives, startup settings and sprite uploads

Local candidate, October 1, 2026. Build ID `452-v1.1-rc26`. Not published or
installed on a Vita. Fixed 30 FPS pacing, shared three-core scheduling and the
accepted 500/444 MHz clock policy remain enabled.

## Startup settings

Hold **D-pad Down + Cross + L + R during boot**. The loader samples the chord
for 1.5 seconds before initializing the game. Settings use a software framebuffer
and the embedded font; no game constructors, shader compiler or game threads run
while the screen is open. `userdata/port_menu.txt` can open it without the chord.

The screen shows the selected OBB filename, data folder, bytes/MiB and readable
RSB version/resource-group count. This metadata is informational. Up/Down selects
an option; Left/Right changes an option. Cross enters coin/gem editing; Left/Right
selects a digit and Up/Down changes that digit. Cross accepts the staged value.

**Done applies pending changes and exits the process. Relaunch to play.** Circle
exits without applying pending changes. Staged edits are retained independently
for up to sixteen players. There is no optimization toggle: the sprite upload
cache and existing optimizations are enabled by default.

**Language override defaults to Off, requesting English (`en_US`, `en`, `US`).**
This leaves mods that translate the English assets on their intended path.
Enabling the override makes the next row selectable. The stock supplied archive
has six complete UTF-16 translation tables, each containing 4,295 string keys:

| Language | Locale | Table bytes |
| --- | --- | ---: |
| English | EN-US | 742,974 |
| German | DE-DE | 807,004 |
| Spanish | ES-ES | 787,518 |
| French | FR-FR | 828,062 |
| Italian | IT-IT | 811,266 |
| Brazilian Portuguese | PT-BR | 778,470 |

No Russian locale table was found in the indexed archive. All six tables were
decompressed and decoded locally; translated content was checked. Custom mods
may omit stock alternate locales: requesting one does not manufacture its assets.
The setting updates all four Java locale/language/country getters consistently.

## Save editing

Only the selected PlayerInfo `objdata.c` and `objdata.g` numeric tokens are
replaced in `userdata/No_Backup/pp.dat`. RTON objects, arrays, string references,
other profiles, unlocks and unrelated bytes stay intact. Values range from zero
to 999,999,999. Coins/gems are supported; no plant/world/gauntlet edits are added.
Encrypted or unsupported saves are refused without alteration.

An edit writes and verifies a unique `pp.dat.backup-N` containing the exact
original bytes, then writes/syncs/verifies a temporary edited file. Original
contents are checked again before commit. Two renames preserve an `.editor-old`
original across interruption. Startup restores it if `pp.dat` is absent.
Settings use a separate recoverable temporary/old-file transaction. Read/write,
sync or commit errors keep the screen open with the error. A user can restore an
earlier save by copying their chosen backup back to `pp.dat` with the game closed.

## Mod archives

OBB preflight no longer opens or validates an expected archive size, header,
hash or stock fingerprint. The build and staging scripts do not enforce the
stock OBB hash, and a VPK can be compiled with no game data present.

The bundled stock index was removed from the VPK and runtime. Header/size-only
binding could otherwise reuse incorrect resource names/offsets for same-size
mods with an unchanged RSB header. Each boot indexes the installed archive on
the existing background worker. This can add startup scanning time compared
with RC25's stock-only shortcut. Persistent decompressed blocks still validate
their actual source/output bytes so changing a mod cannot silently reuse stale
cached content. Structural bounds/decompression checks remain required to parse
the archive. Native library version/fingerprint checks still protect the port's
fixed 4.5.2 call/patch addresses; this is not support for arbitrary game versions.

## Performance and evidence

The new bounded sprite uniform cache suppresses repeated single sampler/int and
float vec4 uploads, including scalar/vector aliases. It queries the actual bound
program and verifies linked uniform types, sizes and sampler limits before
caching. Changed values/programs, arrays, invalid types/locations/units and
nonfinite values preserve the driver path. Relink/delete invalidate metadata;
each frame invalidates uploaded values after loader-owned rendering. Other setters
invalidate the affected value to protect permissive-driver aliasing. There is no
additional worker, texture allocation or skipped zombie simulation.

59 local host/compiled ARM suites pass. New suites exercise synthetic repacked
OBBs, including same header/size with different names/offsets; all nine distinct
local saves; exact backup bytes and commit/sync failures; interrupted-save and
settings recovery; staged versus committed UI behavior; the actual rendered
settings screen; six locale pairs and override-off English behavior. The shader
fixture suppresses 20,000 repeated sampler/color uploads while checking dynamic
values and cache invalidation. Existing math/imports, memory, shader lifetime,
graphics, input, audio, synchronization and cadence regressions pass.

These checks do not measure Vita FPS or prove that busy zombie waves sustain
30 FPS. Earlier device logs showed significant native game/update time. Physical
Vita testing must still check this boot chord, Done/relaunch, selected locale,
save/load, compatible modded OBBs and a heavy wave. No original game/save files
were changed or included in the VPK.
