# RC19: measured CPU texture-preparation optimization

Private candidate, 2026-09-14. PC native benchmarks and bounded ARM instruction tests only. No Vita, Vita3K, game execution, GPU timing, device installation or external publication. Default 30 FPS and persistent user-selected 60 FPS remain unchanged.

## Changes selected by measurement

1. ETC1 decoding builds eight RGBA palette colors per block, instead of recalculating RGB saturation for all 16 pixels. This reduces the number of RGB clamps from 48 to 24. Both ETC1 orientations and differential/individual modes retain byte-identical output, including malformed differential inputs handled by the old decoder.
2. Complete decoded ETC1 blocks copy four contiguous rows instead of running the general per-pixel clipping loop. Edge blocks and worker slices retain the checked path.
3. RGBA half-size conversion sums pairs of byte channels in independent 16-bit lanes. Each lane can hold the maximum sum without carry into its neighbor. Masks preserve exact integer floor averaging. Fixed-size memcpy operations retain support for unaligned source and destination buffers.
4. ETC1 and RGBA kernels are separate functions; dispatch happens once per worker row range. This keeps their stack and register requirements out of alpha conversion. An intermediate build improved PC ETC1/RGBA time but increased ARM alpha instruction counts slightly; the final split removed that regression.
5. Alpha mode/null decisions are made outside the inner pixel loop and output uses an alignment-safe packed RGBA write. The first split still regressed PC alpha downscaling; this final step removed that regression in the repeated large-texture measurements.

No extra texture cache, image buffer or helper thread was added. ETC1 adds a 32-byte local palette. The output size, texture quality, upload format, worker wake/completion protocol, CPU/GPU clock policy and parallel-job threshold are unchanged. Faster CPU preparation can reduce upload/loading work; these tests do not establish a game-wide FPS or temperature gain.

## Method

- Host: AMD Ryzen 5 7500F, six physical cores / twelve logical processors, Windows 11.
- Compiler: installed MinGW GCC 16.1.0, `-std=gnu11 -O2 -static -pthread`; production pixel conversion code, with host pthread-backed substitutes for Vita semaphores.
- Baseline kernels are extracted from the immutable RC18 source archive. Candidate kernels are compiled from current production source. The same harness, compiler settings and input-generation seed are used.
- Input sizes: 64, 256, 1024 and 2048 square pixels. Modes: RGBA half-size, alpha expansion, alpha half-size, ETC1 expansion and ETC1 half-size. Input contains varied deterministic bytes and mixed valid ETC1 blocks. Input is deliberately unaligned.
- Each invocation initializes the worker pool, warms up, calibrates a batch to at least 50 ms, then measures a batch using a monotonic timer. Timed calls reuse preallocated input/output buffers. Allocation and GPU upload are outside this measurement.
- Compare zero helper threads with two helpers plus the caller. Final comparison alternates baseline/candidate execution order and worker-count order across five rounds. Each case reports its median and all five raw samples; no speedup is inferred from a single fastest sample.
- Benchmarks run sequentially, without concurrent builds or test suites initiated by this task. Ordinary background host activity is not controlled. Windows scheduling and host hardware differ substantially from Vita; do not transplant scheduling thresholds or convert these timings into Vita FPS.

Final timing table and full matrix are in `out/rc19-evidence/paired-final.json`. Development comparisons (`pixels-palette.json`, `pixels-candidate.json`, `paired-pixels.json`, `paired-split.json`) describe intermediate source and must not be presented as final-build results. `pixels-final.json` identifies the compiled final benchmark and its evidence directory. Its one-round preparation pass was followed by the five-round alternating comparison below.

Final 2048x2048 median time, with two helpers plus the caller:

| CPU operation | RC18 | RC19 | Less time |
| --- | ---: | ---: | ---: |
| RGBA half-size | 0.846 ms | 0.701 ms | 17.1% |
| Alpha expansion | 0.978 ms | 0.509 ms | 48.0% |
| Alpha half-size | 0.325 ms | 0.265 ms | 18.3% |
| ETC1 expansion | 7.257 ms | 3.470 ms | 52.2% |
| ETC1 half-size | 6.522 ms | 4.408 ms | 32.4% |

Single-thread reductions on those cases were 20.9%, 49.0%, 10.1%, 51.5% and 30.7%, respectively. These are reduced processing times, not percentage gains in game FPS. The full 40-case matrix and raw samples are retained, including smaller inputs and host scheduling variation.

## ARM validation

`check-pixels-arm.py` executes the actual `pixel_rows` kernel from the built ELF against an independent Python reference. It covers five modes, odd/full dimensions, deliberately unaligned buffers, partial worker row ranges and guard bytes. It does not boot the game or emulate a console. The checker supports mocked memcpy imports, but neither measured build needed any in these cases.

For the 64x64 partial-row cases, RC18 to final RC19 instruction counts are:

| Mode | RC18 | RC19 |
| --- | ---: | ---: |
| RGBA half-size | 42,310 | 23,788 |
| Alpha expansion | 52,180 | 22,234 |
| Alpha half-size | 19,504 | 11,796 |
| ETC1 expansion | 289,125 | 139,250 |
| ETC1 half-size | 222,203 | 160,828 |

These are instruction counts, not hardware cycles, cache-miss measurements or power readings. The final ARM compiler is the project's VitaSDK GCC 10.3 softfp toolchain, not the host compiler.

## Reproduction

```
py -3.12 scripts/bench-pixels.py --baseline out/builds/v1.1-rc18/source.zip --output out/rc19-evidence/pixels-before.json --rounds 5
py -3.12 scripts/bench-pixels.py --output out/rc19-evidence/pixels-final.json --rounds 1
py -3.12 scripts/compare-pixel-bench.py out/rc19-evidence/pixels-before.json out/rc19-evidence/pixels-final.json --output out/rc19-evidence/paired-final.json --rounds 5
py -3.12 scripts/check-etc1-block.py
py -3.12 scripts/check-pixel-workers.py
py -3.12 scripts/check-pixels-arm.py --loader-elf build-vita-direct/pvz2_loader
py -3.12 scripts/check-port-suite.py --output out/rc19-evidence
```

The frozen RC18 block decoder lives in `scripts/fixtures/etc1-block-reference.h`. The block test compares 200,000 mixed blocks, both valid and arbitrary bit patterns. Existing pixel-pool checks also exercise odd image sizes, reference downsampling, null alpha data, output capacity, concurrent callers, delayed worker startup, lost notifications and semaphore creation failure.

All **50 local suites passed**, including real supplied-OBB index/decompression checks, corrupted/truncated resource-cache repair, boot-screen rendering, loader allocation failures, math imports, audio/input, JNI, condition destruction, thread attributes, synchronization/deadlines, graphics allocation/storage/program lifetimes, saves/preferences and stall observers. One older condition-destruction harness lacked mocks for the newer sync-observer wrapper; its unmapped-fetch failure was corrected by adding those observer hooks. The real compiled destructor, bridge error translation and retry behavior remain under test. This was a fixture correction, not a newly discovered game crash. Full results and package hashes are in the RC19 archive.

Unresolved hardware reports remain as documented for RC18. No new claim of definitive Zomboss/Zen Garden crash elimination, stable 60 FPS everywhere or measured cooling is made.
