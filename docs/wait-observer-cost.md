# Kernel-wait observer overhead

The native wait wrappers record a thread's blocking primitive so a hardware
stall can be diagnosed. Previously both entry and completion queried the kernel
thread ID and searched the 40-slot table. Sequentially consistent loads also
generated two ARM memory barriers for each examined slot.

The wrapper now retains the returned slot index on its stack and passes it to
completion. Slot identity loads use acquire ordering, paired with publication;
completion publishes the cleared wait state with release ordering. A live thread
retains its slot throughout the wrapped call. Exhaustion returns `-1`, which
completion ignores. No emulated TLS, allocation, clock, new lock, or additional
kernel call is introduced. Kernel arguments, result codes and timeouts are
unchanged.

## Compiled evidence

`scripts/check-stall-overhead-arm.py` executes the linked ARM semaphore wrapper
and observer with kernel calls mocked. It checks that the wait record is present
during the real-call boundary, cleared afterward, and that the kernel error
return survives. Identical starting tables place the current thread at different
slot indices:

| Slot index | Old/new instructions | Old/new barriers | Old/new thread-ID calls |
| --- | --- | --- | --- |
| 0 | 74 / 62 | 7 / 3 | 2 / 1 |
| 5 | 164 / 102 | 27 / 8 | 2 / 1 |
| 15 | 344 / 182 | 67 / 18 | 2 / 1 |
| 39 | 776 / 374 | 163 / 42 | 2 / 1 |

These are instruction and call counts, not Vita elapsed time or FPS. The kernel
implementation is excluded. Raw evidence and pre-change source are preserved in
`out/wait-overhead-evidence`.

The host observer regression retains freeze/recovery, 30-second moving-frame
samples, slot reuse and failed-start cleanup checks. It also verifies full-table
failure and that completion makes no thread-ID query. The compiled native-wrapper
check verifies token propagation, semaphore/mutex/GPU/read calls, error returns,
mutable timeout pointers, and the 64-bit positioned-read ABI.

## Runtime comparison protocol

`scripts/compare-vita3k-title.py` requires matching save hashes, assets, emulator
and settings. It selects frames 900–4200: twelve 300-frame reporting windows
without touch input, texture uploads or pixel-worker jobs. It compares reported
main-thread input plus game work, excluding vblank presentation time. The scene
must also be visually confirmed as the title screen.

This controlled emulator scene cannot establish a gameplay or physical Vita FPS
improvement. The loading-stall root cause remains unproven.

The foreground-controlled pair (`rc7-wait-baseline-c`, `wait-token-d`) measured
median work of 6628.0 and 6591.5 microseconds; means were 6658.17 and 6618.58.
That is about 0.6%, within observed run variation. An earlier baseline median
was 6472.5 microseconds. Therefore this comparison does **not** establish a
meaningful whole-scene speedup. The removed lookup and ARM work are confirmed.

The first changed-build run (`wait-token-b`) is excluded: its copied OBB differed
by one byte at offset 270132669 (`0x0e` became `0x1e`) within
`STREAMINGWAVES/760446319.WEM`. Its manifest recorded the wrong SHA before launch;
the original and repeat copies match the expected hash. The cause is unknown.
The new lab preparation guard rejects that preserved copy and verifies both game
assets before a run becomes active. This is not evidence that the user's Vita
archive has the same problem. The mismatch is recorded in
`out/wait-overhead-evidence/archive-mismatch.json`.
