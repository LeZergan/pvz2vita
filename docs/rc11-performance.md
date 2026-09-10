# RC11 performance work — 10 September 2026

Evidence: RC10 hardware loader.log, copied from the synced project folder to
`out/rc10-performance/loader.log`. It reaches 15,300 frames without a STALL
report. The user reports improved playability with remaining stutters.

| Window ending at frame | FPS | Maximum frame | Link time in window |
| --- | ---: | ---: | ---: |
| 9600 | 48.76 | 621.3 ms | 599.1 ms |
| 11100 | 40.22 | 43.2 ms | 0 |
| 11400 | 44.64 | 60.0 ms | 0 |
| 14400 | 31.20 | 1238.0 ms | 0 |
| 14700 | 23.39 | 849.4 ms | 0 |
| 15300 | 52.57 | 611.2 ms | 595.0 ms |

The 40.22 FPS window spends 23.92 ms/frame inside the native game draw/update
call, with sampled draw submission averaging 2.35 ms. It has no uploads or
links. Shader compilation cannot explain that sustained slowdown. The two
longer transition spikes with zero link time also remain unattributed.
Window elapsed time exceeds accumulated input/game/presentation timing by
about 21–52 ms per 300-frame report; this includes statistics collection and
synchronous reporting. The log does not separate those two costs.

## Implemented

- Exact vertex/fragment source-pair reuse within one running session. An
  unlinked native holder program keeps compiled shaders alive after game
  programs are deleted. On a matching pair, the game program gets those
  shaders and still runs VitaGL's normal linker and per-program attribute
  mapping. This skips postponed GLSL translation/compilation on a cache hit.
  No disk binary restoration, background GL calls, or program-handle aliasing.
- At most 16 holders, 256 KiB of copied source and 1 MiB of estimated serialized
  shader content. This is a content budget, not an exact allocator-residency
  measurement. Failed allocation, missing source, failed linking and full
  capacity retain normal compilation. Mutating a retained shader invalidates
  its pair. No eviction adds a GPU-finish wait. Retention lasts until exit.
- Periodic frame reports enter a two-slot SPSC queue. The existing observer
  writes them once it wakes, off the renderer. A full queue drops reports
  instead of waiting for storage; REPORTQ records the cumulative drop count.
  Failed writes are retried. Boot/fatal messages remain synchronous. If the
  observer cannot start, periodic reports are disabled rather than blocking
  the renderer. Source counters/statistics are still collected on the renderer.
- SHADERS reports pair_hit/miss/bypass alongside link time, allowing hardware
  logs to establish whether the recurring programs actually match.

## Validation and limits

Production C tests cover exact source boundaries, pair changes, shader name
reuse/deletion, retained ownership, source mutation, full capacity, allocation
and link failure. The linked ARM test executes the shipped VitaGL create,
attach, link, query and deletion routines with synthetic compiled shader
fixtures and GPU services mocked. It verifies skipping the translator, correct
reference counts, different attribute locations for different programs, and
no additional GPU finish. It does not render pixels or run the real compiler.
The installed library is authoritative: the local VitaGL source checkout has
some newer internals and is not sufficient by itself to verify its ABI.

The production telemetry test sends 500,000 concurrent reports and verifies
complete ordered payloads, saturation, locked/failed storage, retry and index
wraparound. Program lifetime/binding, observer and texture-worker regression
tests pass, including all pixel completion notifications being suppressed.

No Vita3K was used. No RC11 hardware result is available yet. First-time shader
compilation still costs time; the RC10 log does not contain source fingerprints
to prove that all late links repeat. These changes must not be presented as a
measured hardware FPS increase, a locked 60 FPS fix, or a fix for every stutter.
