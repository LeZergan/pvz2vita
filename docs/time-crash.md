# RC6 timezone crash and runtime ABI fixes

## Confirmed crash

The September 8 device report contains an 8,003-byte RC6 loader log and a Vita
core dump. The build is `452-v1-rc6`, September 7 at 17:51:02. The main thread
data-aborts at `libPVZ2.so + 0x81e9e0`, reading `[r0 + 36]` with `r0 = 0`.

The preceding instruction calls `localtime` with the signed 32-bit timestamp
**6**, six seconds after the Unix epoch. The retained RTC structure on the
stack contains **1969-12-31 20:00:06**, so this conversion used UTC−4. Vita's
newlib converts UTC to local calendar fields, then tries to convert that
pre-1970 local date back through `sceRtcGetTime_t`. That conversion fails;
newlib returns NULL. The Android game immediately reads `tm_gmtoff` from it.

The caller's LR resolves into the matched RC6 `localtime_r` error-return path.
The linked ELF uses text base `0x81000000`, while this core loaded it at
`0x81074000`; symbol lookup must account for both addresses.

This explains why successful sessions did not prove this call safe: the
failure depends on reaching this date conversion with an epoch-adjacent value
and a negative local offset. Other dates or offsets can pass the SDK conversion.
The report does not establish the timezone or exact date-call path of earlier
successful sessions.

There is a second independent ABI defect: Android's ARMv7 `struct tm` has 44
bytes, including `tm_gmtoff` at byte 36 and a zone pointer at byte 40. This SDK's
native structure has only the nine standard fields. Returning it directly
also permits out-of-bounds timezone reads when the conversion succeeds.
Both platforms use a **32-bit time_t** in this build; its width was not the bug.

## Changes in 452-v1.1-rc1

- Route local/UTC conversion, calendar normalization, date formatting/parsing
  and the corresponding mapped 64-bit entry points through an explicit Android
  structure. Compile-time assertions pin the ARM layout.
- Use signed calendar arithmetic for pre-epoch dates and normalize invalid
  month/day/time values before any table lookup. Reject representational
  overflow instead of wrapping it.
- Read the console's current RTC offset without converting pre-1970 local
  dates through `sceRtcGetTime_t`. A failed timezone query retains the last
  valid offset, using UTC until the first success, and logs one warning.
- Keep returned buffers per thread. Format timezone offsets and epoch seconds
  in the bridge. Parse numeric/UTC/GMT timezone forms and epoch seconds, which
  this linked SDK parser does not support. Ordinary date formats use native
  parsing with an explicit structure copy.
- Log one epoch-conversion result at boot so new device reports show the
  actual offset and 44-byte structure.
- Make `clock_gettime(CLOCK_REALTIME)` use the same Unix clock as
  `gettimeofday` and absolute wait deadlines. The old RTC epoch constant was
  9,506 seconds early. Validate clock IDs and permit a null `clock_getres`
  output pointer.
- Stop `statfs` clearing 128 bytes into Android's **88-byte** result. Query
  actual storage capacity/free space, including full or unavailable media,
  instead of always reporting 2 GiB free.
- Correct the directory-entry layout; avoid conversion of an uninitialized
  entry at end-of-directory; give concurrent callers separate output buffers;
  remove per-entry heap allocations and terminate long names.
- Reject incomplete cache reads/writes and close failures; release allocations
  on failure. Check that a rename source exists before entering newlib's
  destination-removing rename implementation.

## Verification

`check-time-arm.py` executes the actual game instructions around the fault and
the linked Vita implementations with injected RTC syscalls. With the old ELF
it reproduces the same NULL read. With the new bridge it proceeds past that
instruction and returns the expected local timestamp. It also reproduces the
old storage query's 40-byte overwrite and verifies the new 88-byte boundary.

The checks include 19,643 independently checked calendar/timezone cases, 54
compiled ARM timezone round trips in the original check (now 77), epoch/2038 boundaries, leap days, malformed
fields, 64-bit overflow, eight concurrent time callers and directory scans,
RTC failure/recovery, parsing and formatting, full/unavailable storage, long
names, end-of-directory, allocation failures and incomplete file I/O.

These are isolated CPU tests, not a game or Vita3K session. The September 9 RC1 Vita log confirms the UTC-4 epoch conversion and reaches
8,400 frames before a separately investigated [transition stall](level-transition-stall.md). The fixed offset follows the console's RTC;
the bridge does not provide Android's historical IANA timezone database.
The rename preflight prevents the missing-source case; it does not make Vita
newlib's replacement operation power-loss atomic. Keep save backups.

The supplied RC6 log shows both texture workers completing jobs on separate
cores, no audio queue starvation, and substantial free memory before the
fault. It does not prove sustained frame rate or full-game stability.

## Sources

- [Android's ARMv7 time layout](https://github.com/aosp-mirror/platform_bionic/blob/android-6.0.1_r1/libc/include/time.h)
- [Vita newlib localtime_r](https://github.com/vitasdk/newlib/blob/master/newlib/libc/sys/vita/lcltime_r.c)
- [Android statfs layout](https://github.com/aosp-mirror/platform_bionic/blob/android-6.0.1_r1/libc/include/sys/vfs.h)
- [Android directory-entry layout](https://github.com/aosp-mirror/platform_bionic/blob/android-6.0.1_r1/libc/include/dirent.h)
- [Vita storage query](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/io/devctl.h)

Core dumps, logs, game binaries and saves are kept out of the source repository.

## September 9: five additional user dumps

All five supplied dumps stop the main thread on the same data abort at
`libPVZ2+0x81e9e0`, with `r0=0`. After accounting for each loader's relocated
text base, every LR is `loader+0x8e9f1`, the archived RC6 `localtime_r` error
return path. The module fingerprint and segment sizes also match the earlier
RC6 report. These are evidence of the old failure path, not RC4 runtime failures;
the dumps do not provide a full installed executable hash.

| Dump timestamp | Input seconds | Retained local RTC date | Offset |
| --- | ---: | --- | --- |
| 1788920187 | 57 | 1969-12-31 17:00:57 | UTC-7 |
| 1788920222 | 6 | 1969-12-31 17:00:06 | UTC-7 |
| 1788920258 | 6 | 1969-12-31 17:00:06 | UTC-7 |
| 1788920884 | 6 | 1969-12-31 21:00:06 | UTC-3 |
| 1788921085 | 6 | 1969-12-31 21:00:06 | UTC-3 |

The retained RTC structures are at the stopped stack pointer minus 32 bytes.
All gzip streams validate, including the three `.tmp` files. Those three have
unavailable trailing ELF segments, but retain the module, thread, registers,
and stack records needed for this diagnosis. The two final dumps are complete.

The expanded `check-time-arm.py` now resolves `localtime` from the actual linked
import table and checks that it points to the tested function. It reproduces
each distinct offset/timestamp pair against RC6 and executes the same game
instructions successfully against the existing `452-v1.1-rc4` fix. The broader
matrix covers 77 timezone round trips. The 19,643 host calendar cases and
concurrent-caller checks also pass.

No further runtime patch is needed for this batch. Canonical packaging was
rerun; the current ELF and `eboot.bin` are byte-identical to the preserved RC4
artifacts. Install `out/pvz2-vita-latest.vpk` over the existing app, retain
`ux0:data/pvz2/` and `userdata/`, and replay the affected actions. This batch
does not establish physical Vita success with the fixed build or resolve the
separately reported transition stalls.
