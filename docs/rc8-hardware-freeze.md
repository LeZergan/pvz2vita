# RC8 hardware white-flower freeze

Both supplied logs identify `452-v1.1-rc8 Sep 10 2026 02:11:49`.
Preserved evidence: `out/rc8-hardware-freeze/loader.log` (SHA256
0f1e2016c0b705314409abaf801382f47750ec0dd203c6b69e753d43a4cb90bb)
and `tester/loader.log` (SHA256
4e7e16e96b848c2e74318e1097c4544bab26be2e9c16b3dfbe15dcaf46d890ac).

Confirmed: main frame 1477 remains in draw for 5/15-second samples on Max's
device; tester frame 2363 remains in draw for 5/15/30-second samples. Main
waits on kernel semaphore 0x40010385 / 0x4001038f. Audio-related worker run
clocks continue. User reports normal audio during the frozen screen.
Tester startup also has a separate five-second sample followed by RESUMED;
that recovered startup sample is not the later freeze.

Normalize loader addresses against the exact archived RC8 ELF, not raw runtime
addresses. The logged pixel_worker entry establishes text relocations +0x59000
and +0x6b000 respectively. Both wait return addresses normalize to 0x8102c09b,
inside pte_osSemaphoreCancellablePend. That common SDK helper does not identify
the enclosing condition. The preceding Android bridge caller is stale when
bridge=none; it must not be used as the blocked caller.

Max has three pixel workers (worker mask 0xe0000); tester has two (0x60000).
Both have idle-looking pixel-worker semaphore waits during the freeze. This
rules out the extra core being necessary for this pattern, but does not prove
a lost pixel completion wakeup. No blocked read or GPU-finish is observed.
The exact higher-level wait and root cause remain unknown.

RC9-WAIT-DIAG retains wait/timedwait/signal/broadcast condition pointers,
mutex pointers and callers separately from both the Android bridge and kernel
wait records. It logs pixel condition addresses and a runtime symbol anchor.
All records are atomic; the observer takes no game lock and never dereferences
the sampled condition pointers. Original waits and deadlines are unchanged.
The existing semaphore observer's hot path and slot layout remain unchanged.

Validation: host observer sampling/reuse/exhaustion/context lifetime; 20 linked
ARM condition wrapper argument/error cases; existing linked native wait ABI
cases; pixel conversion/concurrency and audio/input regressions passed.
This is a diagnostic build, not a confirmed stall fix. No successful RC9 game
boot or hardware test is claimed. User explicitly stopped Vita3K testing;
process 392 was stopped and future work must use hardware evidence.

Next hardware run: install only the diagnostic VPK, reproduce the flower,
leave it for 35 seconds, and retrieve loader.log (plus userdata/stall.log if
available). The new SYNC entries should identify the enclosing condition call.
