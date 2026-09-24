# RC18: asset concurrency, JNI lifetime and controls

Private candidate, 2026-09-14. No console installation, Vita execution, Vita3K run, GitHub publication or Drive upload in this pass. Build marker `452-v1.1-rc18`. Default remains 30 FPS; an explicit 60 FPS selection stays selected, including after restart. CPU policy may change clock frequency but never the selected FPS.

## Confirmed fixes

- JNI direct buffers now have independent tracked wrappers and local/global lifetimes. RC17 returned the native address as the object and kept capacities in a 256-entry ring; distinct wrappers of the same address aliased and live capacities could be overwritten. The same host harness fails the archived RC17 object-identity assertion and passes RC18 with 1,024 live buffers. Wrapper destruction does not free the caller's native payload.
- Virtual asset reads pin both their virtual slot and real descriptor before releasing the table lock. Close marks the slot as closing, rejects new reads, and waits for in-flight operations before recycling it. This covers cached and temporary descriptors. Shared-position reads and seeks serialize; positioned reads and other assets can proceed independently. Waiting operations reject a recycled slot generation. Signed seek overflow, oversized read counts and interrupted short reads are handled.
- The virtual file table uses a sleeping pthread mutex/condition instead of an unbounded spin loop. Contended threads no longer busy-spin while another thread opens a file. Cached reads avoid the prior path formatting/copy and second lock acquisition. This reduces avoidable CPU work; it is not a measured temperature or FPS result.
- Asset buffer loading preserves the logical read position. After materialization, reads/seeks use that same buffer and the stdio handle is released. No extra asset cache or full-asset allocation is introduced. Descriptor handoff preserves an independent asset cursor and reopens only when a later stream read needs it. Failed allocation/read/seek paths do not consume logical bytes; null filenames and overlong paths fail cleanly. Empty reads and bounded seeks have explicit behavior. The asset-manager singleton no longer allocates unchecked memory or copies a live mutex.
- Missing optional assets no longer trim cached FDs and retry an identical failed open. FD exhaustion still gets one trim/retry. Directory pointer arrays grow geometrically, with cleanup on allocation failure.
- RSB lookup normalizes/finds once. Resource extraction and integrity validation use a separate cache mutex, so unrelated index lookups and uncompressed resources no longer wait behind decompression/disk I/O. The loaded index remains immutable. Published cache-path strings are not rewritten. Source/output CRC checks remain enabled.
- Controller Start/Back state is tracked separately from physical button history, ensuring key-up delivery when touch, L+R, settings, or a modal takes ownership. Neutral sticks are required before buttons regain ownership. Seed focus remembers the last chosen slot; Circle cancels tray focus and returns to the previous lawn position. D-pad diagonal speed matches axial speed. Existing touch input, fine right-stick pointer, Cross dragging and settings chord remain.

## Local evidence

`out/rc18-evidence/checks.json` records 28 passing checks, including compiled ARM instruction checks for allocation guards, shader pairs, time, placement, texture storage, graphics cleanup and JNI locking. These are bounded instruction tests, not a game or console emulator session.

- `check-asset-vfd.py`: deterministic blocked-reader tests for concurrent shared offsets, close during cached/evicted reads, unrelated reads proceeding, FD leak checks, short reads/EINTR, count and seek bounds.
- `check-asset-manager.py`: actual host files, buffer/descriptor cursor semantics and fault injection. 1,000 buffered read/seek pairs make zero stdio calls after materialization; a 1,000-entry directory requires seven pointer-array growth allocations, versus the prior one per entry. These are operation counts, not an FPS benchmark.
- `check-rsb-concurrency.py`: 20,000 unrelated lookups complete while a mock extraction is deliberately blocked; initialization runs once and cached paths remain stable. Actual decompression is not simulated by this test.
- `check-jni-lifetime.py`: 1,024 direct buffers, 400,000 threaded transient objects, 20,000 simultaneous references, pool recycling and balanced destruction. Archived RC17 comparison is saved in `rc17-jni-regression.log`.
- `check-controller.py`: input edges, pointer ownership, seed return/cancel/remembering, chord, modal failures, cursor GL-state restoration and frame budgets. `check-fps-preference.py` and `check-power-policy.py` retain persistence and clock-policy coverage.

The archive contains matching VPK, unstripped ELF, SELF, source snapshot, build log, checks and hashes. Canonical candidate: `out/pvz2-vita-latest.vpk`; tester copy: `Tester/pvz2-vita-latest.vpk`.

## Upstream review

Read/fetched [PvZ2Native](https://github.com/OptiJuegos/PvZ2Native) at `0feee44402a00e9c68da98bcfc403f03f8593043` and [the Vita port](https://github.com/LeZergan/pvz2vita) at `d2f51afeb982ec17fc2a2a60cf7548afd99eb9ca`. The newest native upstream commit removes decompiled Java files; it does not add a native frame/input/runtime optimization. No merge was made and no local decompiled files were removed. Native input queue, lifecycle, frame dispatch and patches were inspected. Lifecycle events are queued and pumped through draw; adding unverified pause calls would need game-level validation.

[Issue 3](https://github.com/LeZergan/pvz2vita/issues/3) reports increased CPU use in pause menus on RC11, but does not establish GPU time, frame timings or a root cause. [Issue 1](https://github.com/LeZergan/pvz2vita/issues/1) concerns an older loading freeze. Neither proves this candidate fixes those runtime reports. Asset contracts were checked against the [Android NDK reference](https://developer.android.com/ndk/reference/group/asset).

## Remaining uncertainty

No definitive root cause has been established for the reported new Zomboss/Zen Garden freeze or console-wide “corrupted file” errors. The supplied dump hashes still match previously analyzed RC6 time faults; they cannot prove a failure in this new build. No additional loader log was requested or pulled. Prior texture-pressure and audio fixes are retained and checked locally. Hardware temperature, real GPU utilization, disappearing-map reproduction and sustained 60 FPS remain unmeasured. No claim of all crashes eliminated, release readiness, or a quantified FPS/temperature improvement is justified without runtime evidence.
