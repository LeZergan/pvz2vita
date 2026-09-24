# Plants vs. Zombies 2 — PS Vita 1.1 RC25

Install `pvz2-vita-latest.vpk` over the previous loader. Keep your existing
`ux0:data/pvz2/` game files and `userdata/` saves. Supported game data remains
Android 4.5.2 ROW / version 147. No commercial game files are included.

## CPU and pacing

- Main thread and helpers can run across all three application cores; removed
  single-core pinning and the main-core reservation.
- Texture conversions use a shared work queue across two helpers and the
  caller, so delayed workers no longer hold a fixed share of the rows.
- Request 500 MHz where the system accepts it; otherwise use 444 MHz. The
  performance floor is 444 MHz. No clock plugin installation/configuration.
- Fixed 30 FPS target with sleeping deadlines and vblank. No 60 FPS override.

## Crash recovery

The four new dumps show allocation-failure aborts: three 4.5 MiB requests and
one 6 MiB request. Failed large game-heap allocations can now use a bounded
16 MiB fallback outside the fixed heap, subject to available USER memory and
an 8 MiB headroom check. Allocation/free/realloc ownership is covered by host
and compiled ARM regression tests. Graphics memory budgets are unchanged.

## Included since the last public release (RC12)

- Synchronized graphics garbage collection, removing a reproduced leak race
  and the now-unnecessary allocator recovery delay.
- Texture update/completeness, audio queue and error-handling fixes.
- Asset descriptor reuse, archive/cache validation and faster CRC checks.
- Controller pointer and seed-tray navigation, Vita keyboard integration,
  pointer ownership repair and analog sampling restoration after dialogs.
- Logging/profiling disabled unless `ux0:data/pvz2/logging/` is manually
  created after setup. Removing that directory disables it again on relaunch.

## Tested and still unverified

55 host/compiled ARM suites pass. The exact game allocation exception is
reproduced and the recovery path is exercised with mocked memory syscalls.
The VPK, SELF, metadata and artwork are checked; symbols are preserved locally.

RC25 has not yet been tested on a physical Vita. **30 FPS is the cap/target,
not a proven heavy-wave minimum.** The allocation fallback cannot solve total
memory exhaustion or every possible crash. Shader compilation can still stall
new scenes; the reported initial-menu blue line remains unresolved.

See [technical evidence](rc25-cpu-memory.md). Test the same heavy wave and crash
route, and include the build ID `452-v1.1-rc25` with new reports.
