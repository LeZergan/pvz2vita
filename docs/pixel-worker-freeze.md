# RC10 texture-worker handoff

The fresh hardware log is RC9-WAIT-DIAG, frame 854, frozen in draw at 5, 15
and 30 seconds. Its startup anchors and SYNC records identify main waiting
on pixel_done (0x81202bf8) and all three texture workers waiting on pixel_start
(0x81202bf4), using pixel_lock (0x81202bf0). The runtime text relocation is
+0x62000. Main caller 0x8107f1f5 resolves to pvz2_pixels_convert's completion
condition wait; worker caller 0x8107efad resolves to pixel_worker's start wait.
The exact log is preserved at out/rc9-hardware-freeze/loader.log, alongside
the previous source. This confirms the blocked subsystem. It does not expose
the SDK condition counters or establish which internal wakeup went wrong.

The old pool couples a shared start broadcast and a completion condition.
RC10 removes both conditions from this pool. Each worker gets its own kernel
notification; an atomic generation publishes the immutable job. Workers read
that generation with acquire ordering, process disjoint rows and decrement an
atomic pending count using acquire/release ordering. The caller acquires zero
before returning the output buffer. No worker accesses the job or buffers after
its decrement. The existing submission mutex prevents overlapping generations.

Notifications only accelerate progress. Workers recheck publication after a
100ms bounded wait; the caller rechecks completion after a 10ms bounded wait.
Normal notifications wake immediately. Lost/coalesced/stale notifications cannot
make a completed job wait forever. The code never forces completion, skips rows,
or frees a buffer still owned by a worker. Semaphore/thread creation failure
reduces the pool or falls back to the same synchronous conversion. Existing
game/audio synchronization is unchanged. Parallel conversion remains enabled.

Validation uses the production C source with a bounded counting-semaphore
test backend. It checks byte-exact RGBA/alpha output, odd dimensions, unaligned
and null inputs, 0/1/2/3 workers, concurrent callers, delayed worker startup,
repeated jobs, immediate buffer release, and partial semaphore/thread creation
failure. Every start and completion notification is deliberately suppressed for
four jobs in each of eight configurations: the jobs still finish correctly.
This fault injection validates the replacement's recovery; it is not a claim
that the internal SDK failure was reproduced on a host. No Vita3K was used.

The VPK must still be tested on hardware at the failing load. No measured
hardware FPS improvement or universal gameplay correctness is claimed.
