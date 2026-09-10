# RC7: Unicode corruption and failed reference allocation

Confirmed with the previous production source and compiled RC6 ARM routines:
the string bridge confused UTF-8 byte counts with UTF-16 code-unit counts.
`NewString` truncated a four-character Cyrillic fixture to four encoded bytes
instead of eight. `NewStringUTF` reported eight UTF-16 units for that same name.
Embedded NUL and surrogate pairs were also converted incorrectly. These are
confirmed compatibility defects; a heap overwrite was initially suspected but
was not established by this replay.

RC7 builds both representations before publishing the string. UTF-16 lengths
count code units; UTF lengths count modified UTF-8 bytes. Getters copy immutable
buffers without reconverting them. JNI's modified UTF-8 preserves NUL and each
surrogate code unit; native four-byte UTF-8 input is accepted and normalized.
The game's ordinary UTF-8 input-event encoding remains separate.

String-region indices now address UTF-16 units. Negative/out-of-range and
overflowing ranges leave output untouched. UTF regions write their encoded bytes
without assuming the caller allocated an extra terminator. These semantics follow
the [JNI string API](https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/functions.html).

Failure injection found another concrete defect: a failed reference-record
allocation returned an untracked object that later releases could not free.
String and array constructors now dispose of the owned object and return NULL,
including releasing object-array child references. Empty dynamic arrays avoid
the ambiguous `realloc(pointer, 0)` behavior that can leave a freed pointer stored
after a reported failure.

## Evidence

- Previous source: `out/jni-unicode-evidence/FalsoJNI-before.c` and
  `Bridge-before.c`. Host failures: `out/jni-unicode-h1fdtx_k/result.txt`.
- Actual RC6 ARM failure: `out/jni-unicode-evidence/rc6-arm-result.txt`.
- `scripts/check-jni-unicode.py`: production constructors/getters/regions,
  Cyrillic/CJK/NUL/surrogates, empty strings, allocation guards, 40,000 concurrent
  reads and allocation-failure cleanup for strings/primitive/object arrays.
- `scripts/check-jni-unicode-arm.py`: actual linked ARM string routines with
  guarded mocked allocation boundaries. No kernel or physical device in this test.
- RC7 results: `out/rc7-unicode.log`, `out/rc7-unicode-arm.log`, `out/rc7-jni.log`.
  Existing JNI lifetime stress still passes 400,000 transient allocations and
  20,000 simultaneous references.

## Targeted work reduction

Identical ASCII calls in the compiled ARM fixture executed these instruction
counts inside `GetStringUTFChars` (eight identical samples per length):

| ASCII bytes | RC6 | RC7 |
| --- | ---: | ---: |
| 5 | 212 | 31 |
| 64 | 1724 | 68 |
| 255 | 6546 | 115 |

Allocator calls are mocked. Including construction plus one getter call, counts
were 690 to 534 (5 bytes), 5072 to 3264 (64 bytes), and 19098 to 11942 (255 bytes),
about 23–37% less work in these bounded cases. Five-byte construction alone rose
from 478 to 503 instructions; the combined reduction comes from the read path.
This is not a measured Vita FPS or whole-game speedup. Raw comparison files are
under `out/jni-unicode-evidence/`.

## Runtime scope

RC7 reached the title under combined slow CPU, enabled surface synchronization,
10 ms file-open delay and Cubeb audio. Name editing was opened. Bulk automated
text input did not deliver the intended Cyrillic or ASCII text; a physical `m`
key did appear. The Cyrillic in-game entry test is inconclusive. An initial
two-byte commit was filtered/truncated by the game and was not a successful name
replacement; no claim of end-to-end Unicode UI correctness follows from it.
Day 4 entry and exit back to title succeeded. Logs were captured and the emulator
stopped; observations are recorded in the `rc7-unicode/run.json` manifest.
Original user profiles were not modified.

The reported infinite loading flower remains unreproduced. These fixes do not
establish later-game/Zen Garden stability or physical Vita performance.
