# Intermittent stalls after finishing a level

## Device evidence

The September 9 report runs `452-v1.1-rc1` with UTC−4. Its epoch conversion
succeeds, confirming the previous timezone fix on this device. The last report
is frame 8,400 at approximately 53 FPS, with about 95.5 MiB of used heap, active
native workers on cores 1/2, and no audio queue starvation. The log then ends.
The user first reported finishing Ancient Egypt level 2 and subsequently other
levels; rebooting sometimes helps.

The log has no blocked-call or stack capture. It does not establish the exact
cause of this particular stall, nor does it demonstrate an out-of-memory event.

## Confirmed defects fixed in 1.1 RC2

The linked Vita pthread library's condition destructor acquires the global
condition-list lock. Two early error returns fail to release it: failure of
the condition gate wait, and a busy/error result from the unblock mutex trylock.
Later condition initialization/destruction can then block on that leaked lock.
This can affect resource/thread teardown between levels.

The correction retains the upstream destructor and releases the list lock on
both exits. It is linked using `--wrap=pthread_cond_destroy`, so the game bridge
and other separately compiled users of this SDK call share the correction.
Busy objects remain registered and allocated; successful destruction removes
their registry entries. No arbitrary wakeups, skipped resource jobs, or timeout
successes were added. The SDK condition layout is asserted at build time.

Two related Android compatibility fixes are included:

- Condition attribute init/destroy imports call their real wrappers instead
  of no-ops. Initialization now produces the native attribute consumed by
  condition creation and destruction releases it.
- Pthread wrappers translate returned errors to Android values, including
  Vita timeout 116 → Android 110 and deadlock 45 → 35. Successful calls keep
  a short path without an error-table search.

Worker diagnostic slots are released on normal return and `pthread_exit`.

## Verification and limits

`check-cond-destroy-arm.py` executes the actual compiled Vita functions with
injected primitive results. The retained RC1 ELF reproduces both global-lock
leaks. RC2 balances the locks, preserves busy objects, permits successful retry,
and passes static-condition cleanup. The check also follows the shipped Android
bridge through the link wrapper, checks attribute allocation/OOM and canaries,
and verifies Android wait-error numbers.

Host production checks cover concurrent initialization and condition wakeups,
8,192 synchronization objects, deletion/reuse, worker creation/affinity, and
observer behavior. These are isolated command-line checks, not a game session.
There is no physical Vita verification of RC2 yet. The lock defect is confirmed;
its responsibility for the supplied level-exit stall remains an inference.

## If another transition stalls

The frame observer runs at low priority and samples once per second. It writes
snapshots at 5, 15 and 30 seconds without a completed frame, then stays quiet
until frames resume. Snapshots identify the frame phase, thread kernel wait
state, and the active observed condition/join/semaphore/fsync/rename call.
It does not instrument individual rendering mutexes, scan stacks, suspend
threads or force a transition. Kernel waits remain visible even when no bridge
breadcrumb is available. A keyboard or legitimate lengthy operation can also
produce a snapshot; a snapshot alone is not proof of deadlock.

Keep `ux0:data/pvz2/userdata/loader.log` and `stall.log` after a problem.
`stall.log` uses a separate writer so a blocked loader logger need not suppress
the report. Each log is limited to 2 MiB plus one previous copy. Preserve any
matching Vita core dump. Do not erase the save to test this fix.

## Sources

- [Pthreads-embedded destructor and original error exits](https://github.com/vitasdk/pthread-embedded/blob/11d2e5722d98c86f33c908fc47b2cf6e55205db5/pthread_cond_destroy.c)
- [SDK condition structure](https://github.com/vitasdk/pthread-embedded/blob/11d2e5722d98c86f33c908fc47b2cf6e55205db5/implement.h)

The vendored correction retains the LGPL notice and license. Device logs, core
dumps, game data and local build symbols are excluded from the source export.

## RC3 follow-up

An audit of the exact linked RC2 image found 32 missing runtime imports. The
resolver silently used an integer-zero stub for them, including double-valued
math and thread attribute functions with output pointers. RC3 supplies the
missing functions and stops at boot if a strong import is unresolved. It also
removes the 128 KiB cap on explicitly requested worker stacks. See the
[compiled import audit](runtime-import-audit.md) for the reproductions and limits.

The newer Zen Garden report contains an RC2 log ending after frame 26,400.
Its final report has workers running on both cores 1/2 and no audio starvation;
the separate `stall.log` is absent. It confirms a reported stall on RC2 without
establishing its blocked call. RC3 adds bounded resource counters to the existing frame
reports to help distinguish a loading screen that still animates from a
thread that stops completing frames. Neither successful builds nor isolated
checks prove that every transition now succeeds on hardware.
