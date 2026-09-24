# RC20: asset descriptor re-caching and open isolation

Private candidate, 2026-09-14. No console/Vita3K run, installation, new device log, Drive upload or GitHub publication. Retains RC19 texture optimizations and the default 30 FPS / persistent explicit 60 FPS choice.

## Confirmed issues and changes

An asset evicted from the 16-entry raw descriptor cache retained `raw_fd = -1`. Every subsequent read opened a temporary file descriptor and closed it immediately. Even a now-active asset could never rejoin the cache. RC20 admits that opened descriptor back into the existing bounded cache when space is available. It trims an idle entry first, adopts under the file-table mutex, and otherwise retains the temporary-descriptor fallback. No asset bytes or new texture buffers are cached.

The existing read pin remains active across reopening, adoption, I/O and completion. Trim cannot close a busy descriptor; close cannot recycle a busy virtual slot. If close already marked a slot as closing, the reopened descriptor remains temporary and is closed by its reader. Simultaneous positioned reads cannot overwrite an already-adopted descriptor; extra opens remain temporary and are closed normally.

Initial file opens previously ran inside the shared file-table lock. One slow storage open could therefore block unrelated cached reads and closes. RC20 reserves a non-public slot under the mutex, performs the open outside it, and publishes only after success. Failed opens release the reservation and return `-1`; the old code returned a virtual descriptor even when its underlying open failed. Descriptor exhaustion gets one trim/retry; missing-file and other errors do not trigger an identical retry.

## PC benchmark

`scripts/bench-asset-vfd.py` compiles the archived RC19 and current production virtual-file functions using the same host adapters and `gcc -std=gnu11 -O2 -static -pthread`. It opens 24 virtual handles so the first asset is evicted, then issues 2,000 positioned 4 KiB reads from that asset. The 1 MiB deterministic fixture is entirely synthetic. Both versions validate returned data and close all handles. The single-thread host adapter implements pread using the same seek/read pair in each build; Vita continues to use its native pread.

Nine paired runs alternate baseline/candidate order. Median results:

| Measurement | RC19 | RC20 |
| --- | ---: | ---: |
| Total time for 2,000 reads | 37.918 ms | 3.571 ms |
| File opens during those reads | 2,000 | 1 |
| File closes during those reads | 2,000 | 1 idle-cache eviction |

The measured batch takes 90.6% less time in this specific host workload. This is an OS-cached PC I/O benchmark, not cold-storage speed, game FPS or a Vita temperature result. It demonstrates eliminated repeated opens; the magnitude of a gameplay benefit is unknown. Raw samples, generated benchmark code, executable hashes/source identifiers and reproduction script are retained in the archive.

```
py -3.12 scripts/bench-asset-vfd.py --baseline out/builds/v1.1-rc19/source.zip --output out/rc20-evidence/vfd-benchmark.json
py -3.12 scripts/check-port-suite.py --output out/rc20-evidence
```

## Verification

Expanded production VFD tests verify that an intentionally blocked open permits another asset's read/close, 600 failed opens retain no slots, FD exhaustion retries once, and 500 reads of an evicted asset cause only one reopen while the cache stays within 16 entries. Cached/evicted read-close races, shared-offset serialization, independent positioned reads, short reads, EINTR, oversized counts, seek overflow and handle-leak checks remain covered.

All 50 local suites are rerun against RC20, including built ARM code, texture kernels, audio, controls, JNI, allocation failures, real supplied-archive decompression/cache repair, threading, saves and FPS preferences. Exact results are recorded in `checks.json`; package verification checks SELF, SFO, resources, build marker and matching archived ELF/eboot/VPK.

The canonical candidate is `out/pvz2-vita-latest.vpk`, with an identical `Tester` copy. Runtime Zomboss/Zen Garden crashes, map disappearance, sustained FPS and heat remain unverified on this candidate; these changes do not establish their root cause.
