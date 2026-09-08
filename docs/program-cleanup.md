# Graphics program cleanup — 1.1 RC4

The game has three cleanup branches at library offsets `0xdb2af4`, `0xdb2b40`
and `0xdb2b88` that call `glIsProgram` before `glDeleteProgram`. The loader's
old validity import always returned true. Zero-initialized or previously cleared
program fields therefore passed that check.

The pinned vitaGL library indexes its program table as `program - 1` without
checking the handle in either query or deletion. A native deletion of zero can
read and write before the table; it can also treat unrelated data as pointers
to resources to free. This is a confirmed loader/driver compatibility defect.
It has not been matched to a physical Zen Garden stack capture.

RC4 maps the import to a real, bounded validity query. Zero and handles above
the validated native table limit return false before entering the driver.
Deleting zero does nothing; deletion of other handles clears loader caches and
only calls the driver when the native program exists. Cache cleanup also runs
when a driver-owned call has already deleted the native object, so reusing that
numeric handle cannot retain old uniform locations.

## Verification

`check-program-lifetime-arm.py` uses the exact locally supplied game library
and compiled loader ELF. With the retained RC3 image, all three original game
branches reach native `glDeleteProgram(0)`. A separate direct execution of the
compiled native function confirms its write before the table. The underflow
measurement is separate from the game-to-bridge call test; it is not a physical
GPU crash reproduction.

With RC4, the same game branches and direct bridge calls reject zero,
out-of-range and deleted handles. Native allocation, valid deletion, repeated
cleanup and numeric handle reuse pass. The test fills all but the last native
slot and invokes the actual creation function to verify the upper boundary.
It also verifies that the shipped import table selects the corrected wrappers.

The guard uses the pinned SDK's 1024-program limit. The check verifies the ELF
table size and behavior; a replacement SDK with a different layout needs a new
assessment. No copies of the native game library are included in source.

Host production checks cover uniform/matrix cache invalidation, raw driver state
changes and stale-handle cleanup. The new ARM regression, import/math audit,
condition destruction, time/storage and Zomboss regressions pass on RC4. These
eight targeted scripts use no game session, Vita3K or desktop control. GPU
completion and frees are mocked in the instruction check. Physical scene exits,
save/relaunch and the reported stalls remain to be tested with RC4.
