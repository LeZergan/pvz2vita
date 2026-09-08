# Runtime import and worker stack audit — 1.1 RC3

## Confirmed defects

The actual RC2 ELF import arrays omit 32 of the supported game library's 341
required imports. The resolver sends those calls to an integer-zero function.
This can report success without initializing outputs, or return an incorrect
floating value. An isolated ARM execution reproduces the old fallback returning
the wrong double for `cbrt(8)`.

RC3 adds the missing math functions with the Android ARMv7 calling convention,
including 64-bit integer returns and output-pointer arguments. Floating-point
classification uses Android's constants and handles signed zero, NaN, infinity
and subnormal values even when the ARM flush-to-zero mode is enabled. The native
`fputwc` and `inet_addr` implementations are also mapped.

The missing thread functions now initialize scheduling and stack query outputs.
Current-thread queries read the Vita kernel's actual stack address and size.
Android's normal priority 0 is translated to the SDK's default rather than
passed into its incompatible native priority range. Detached/joinable values
are translated explicitly and invalid values are rejected.

Thread creation previously reduced requests larger than 128 KiB to 128 KiB and
could retry with a still smaller stack. RC3 preserves larger explicit requests,
uses a local native attribute without changing the caller's initialized
attribute, and retries temporary allocation failures with the same stack size.
Creation failures record the requested stack and error. The existing worker
affinity and cleanup paths remain in use.

The import resolver now reports missing strong imports and stops before game
constructors. It no longer silently assigns unknown calls a success stub. Weak
imports retain their ELF semantics. Absolute relocations preserve their original
addend when a local function overrides a dependency. The supported game has no
undefined absolute relocations; that case is covered with a synthetic fixture.

## Verification

- The compiled RC3 arrays contain all 341 required imports from the exact local
  game library. The retained RC2 image reproduces the 32 omissions.
- 119 isolated compiled ARM math cases cover the actual functions and calling
  convention, including double/64-bit results, output pointers and subnormals.
- Production source checks cover thread output canaries, 512 KiB stack requests,
  unchanged caller attributes, detach state, native priorities and failed kernel
  queries. ARM compilation asserts the Android attribute occupies 24 bytes.
- Resolver checks cover strong failures, weak imports, dependency overrides and
  relocation addends. Earlier condition-lock, timezone and Zomboss ARM
  regressions still pass. There are 25 passing local check scripts in this pass.

These are bounded command-line checks. No game session or Vita3K was launched.
They do not measure physical Vita performance or establish that a specific
reported transition now completes.

## Remaining limits and useful logs

The newer `loader stalled exit zen garden.zip` contains an RC2 `loader.log`
ending after frame 26,400 (about 56 FPS in the final report). Nine workers remain
registered, seven advanced in the last interval on cores 1/2, JNI live references
remain at 15, and audio reports no queue starvation. The final report shows
about 100.9 MiB of heap used and 123.7 MiB free in the vitaGL RAM pool.
It does not demonstrate exhaustion or identify a blocked call. The ZIP does
not include the separate `stall.log`. RC2 therefore still has a reported stall;
the exact wait and whether the RC3 changes resolve it remain unproven.

Vita kernel threads allocate their own stacks. A request to run on caller-owned
stack memory returns `ENOTSUP`; realtime FIFO/RR policies also return `ENOTSUP`.
Normal stack-size requests and normal scheduling are supported. The inspected
EA policy callers have a normal-policy fallback. Complete import coverage does
not imply complete Android compatibility, and existing explicit platform stubs
still need to be assessed against actual game paths.

`[ASSETIO]` reports resource requests, misses, extracted blocks, failures and
active extraction bytes. Its atomic snapshot does not acquire the index lock,
so it can report while another thread extracts a block. Up to 24 asset lookup
failures include the resource name; a lookup miss alone is not proof of a bug.
If frames stop, the separate observer writes at 5/15/30 seconds. Keep
`userdata/loader.log` and `userdata/stall.log` after a failure. Each is capped at
2 MiB plus one previous copy. Saves do not need to be deleted or converted.
RC3 logs successful observer startup and its output path in `loader.log`.

Android classification definitions: [AOSP Bionic math.h](https://github.com/aosp-mirror/platform_bionic/blob/android-6.0.1_r1/libm/include/math.h).
