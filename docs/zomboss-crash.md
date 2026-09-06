# RC4: placement stack corruption in the Zomboss report

## Device evidence

The September 6 RC3 device report (`452-v1-rc3`, built 14:47:43) includes a
31,196-byte loader log and a Vita core dump. The user identified the encounter
as Ancient Egypt Zomboss. The log itself has no scene identifier.

The main thread stopped on a data abort at runtime PC `0x810a0b86`. Using the
matching RC3 ELF and the core's loader text base `0x81015000`, this resolves to
`_free_r+0xe` (linked address `0x8108bb86`). The faulting load is
`ldr r0,[r4,#-4]`; `r4` is **1**. The saved return address is `0x981dc65c`,
immediately after the game's `operator delete` call at **libPVZ2+0x1dc658**.

The native caller's stack pointer is `0x81180388`. Its vector begin pointer,
at stack offset `0x1f8`, contains **1**. This is an invalid free, rather than an
allocation failure reported by the loader. The final log windows show roughly
60 FPS, balanced touch events, JNI_live=15, and no audio queue starvation.
Those measurements do not establish that the full encounter was stable.

## Corruption mechanism

The exact 4.5.2 function at `0x1dbe80` initializes a 9×10 integer scratch grid at
stack offset `0x80`. Placement at `0x1dc8f0` first searches using the full
footprint, then tries relaxed rules, finally searching with a smaller minimum
footprint. The marker at `0x1dca90` always writes the full footprint and does
not check grid bounds.

A minimum 1×1 footprint can fit at `(8,3)` while its full 2×2 footprint extends
beyond the grid. The original marker writes cells 83, 84, **93 and 94**.
Cell 94 occupies `0x80 + 94*4 = 0x1f8`: exactly the vector begin pointer.
The outlying cell is marked 1, so cleanup later calls `delete(0x1)`.

The isolated ARM check runs the original candidate enumeration and reproduces
that fallback, then executes the original marker and cleanup to reproduce
`delete(0x1)`. This matches the core's corrupted field and delete argument.
The core does not retain the exact earlier placement arguments, so `(8,3)` is
the reproduced triggering example, not a claimed trace of that earlier call.

## Fix

`source/utils/placement_452.c` replaces only the native marker block. Candidate
selection, object position calculation, reference lifetimes and cleanup stay
native. The C helper intersects the written footprint with the legal 9×10 grid
and retains the original per-cell maximum rule (3 inside the minimum footprint,
1 outside). It bounds arithmetic and work even for invalid extreme dimensions.

Installation checks every instruction in the replaced block and additional
caller/fallback fingerprints before patching. A mismatch fails startup. The
permanent ARM bridge preserves registers and 8-byte stack alignment across the
C call, reproduces the native `this` spill and offset loads, then jumps to
`libPVZ2+0x1dcb20`. It does not suppress invalid frees or disable the encounter.

`PLACEMENT` diagnostics are limited to the first eight clipped footprints per
process. Existing 2 MiB log rotation remains in effect.

## Validation and remaining device check

- `scripts/check-placement.py`: 57,745 bounded cases using the production
  helper, independent cell-by-cell expected results, neighboring stack guards,
  negative/extreme dimensions and the reproduced pointer overwrite.
- `scripts/check-placement-arm.py`: original ARM candidate fallback and
  marker/cleanup failure, then the actual compiled Vita ARM/Thumb bridge,
  production helper, live registers, stack pointer, preserved delete argument,
  legal cell values, installation and altered-fingerprint rejection.
- Softfp Vita compilation and VPK packaging checks.

The instruction check uses local game bytes and a bounded instruction harness;
it does not boot the game, start Vita3K, display graphics or use saves.

**Pending:** install RC4 on a physical Vita, replay Ancient Egypt Zomboss from
the same progress, finish the encounter, and save/relaunch. Check the resulting
log for `BUILD 452-v1-rc4`, placement patch installation and any clipped-footprint
diagnostics. A passing isolated check does not prove every level crash-free.
