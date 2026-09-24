# RC22 loading performance

The change is limited to the RSB archive's checksum work. A slicing-by-four
IEEE CRC-32 routine replaces byte-serial CRC in index checking, cache validation
and extraction. Source and output checksums remain mandatory. Cache formats,
corruption repair, decompression, locks, worker count and buffer sizes are
unchanged. The routine uses a 4 KiB immutable table, no heap allocation, no
runtime initialization and no alignment-dependent reads. The bundled miniz
library itself is unchanged.

Nine alternating paired PC rounds against archived RC21, using the supplied OBB:

| Operation | RC21 median | RC22 median | Time reduction |
| --- | ---: | ---: | ---: |
| Bundled index load | 1.3705 ms | 0.9960 ms | 27.3% |
| Extract 5 MiB block | 21.2345 ms | 14.9570 ms | 29.6% |
| Fully validate cached block | 11.4042 ms | 4.2755 ms | 62.5% |

The selected block contains 532,480 compressed bytes and 5,242,880 output bytes.
Each extraction starts without that cached output; validation checks both the
source archive and output. PC input reads are OS-cached. These are component
measurements, not end-to-end game loading or Vita SD-card timings.

The separate checksum benchmark processes 128 MiB per sample in the same
16 KiB chunks as production. Nine paired rounds: 234.637 ms to 75.274 ms
(67.9% less checksum time). Actual linked ARM instruction counts for 16 KiB:
110,610 to 73,742 (33.3% fewer). No gameplay CPU percentage, FPS gain or
temperature reduction is established by these measurements.

The ARM regression checks 184 combinations of lengths, alignment and CRC seed
against Python zlib, plus incremental chunking. The full local suite retains
real-OBB record comparison, byte-exact independent decompression, warm reuse,
modified/truncated cache repair and changed-source rejection. All 53 suites
must pass before packaging.

Comment cleanup replaces historical debugging narratives in graphics setup and
runtime logging with current behavior, corrects an obsolete frame-rate comment,
and clarifies the inflate guard. Fixed 30 FPS, controls and manual logging
opt-in remain unchanged. No logging directory is created or packaged.

Reproduction: `scripts/bench-loading-crc.py`, `scripts/bench-rsb-loading.py`,
`scripts/check-cache-crc-arm.py`, `scripts/check-port-suite.py`.
The immutable table can be regenerated with `scripts/generate-cache-crc.py`.
Raw timings, harnesses and symbols accompany the archived build. No Vita or
Vita3K testing or external publication was performed.
