# September 14 Zomboss report

User reports new crashes, then continued play after restarting. No new runtime
log or installed build identification was supplied. Restart recovery alone
does not identify a memory, GPU, save, or threading cause.

Both supplied attachments have the exact SHA-256 of previously analyzed RC6
timezone dumps, including the copies archived in out/rc11-zomboss-report:

- `psp2core-1788920884-0x00024c3e65-eboot.bin (4).psp2dmp`:
  `259a29cc5276bc3fda45eb7e36c658109bcb60bab6555e106222721a804fd494`
- `psp2core-1788921085-0x00031c3349-eboot.bin (3).psp2dmp`:
  `9a4961fb028e68f14f608d9dca46e97a3d51252fbe5f8d080b5d690a0e07c1c0`

Different Windows duplicate suffixes do not mean different crash contents.
Their recorded fault is libPVZ2+0x81e9e0 after the old localtime_r path returns
NULL for timestamp 6 at UTC-3. They cannot locate the newly reported crash.

Rechecked the exact archived RC17 ELF with check-time-arm.py and
check-placement-arm.py against the supplied game library. Both passed: the
time import runs past the old crash instruction; placement retains bounds,
stack/register integrity and a valid cleanup pointer. These are bounded ARM
instruction checks, not console/gameplay validation.

No runtime patch or new VPK is justified by these duplicate dumps. RC17 remains
the current candidate. Need the newly generated crash dump plus the matching
loader log (and previous copy after restart), to establish the affected build
and fault. No Vita/Vita3K testing, device access, save changes or publication.
