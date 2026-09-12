# Plants vs. Zombies 2 for PS Vita — 1.1 RC12

RC12 fixes reproduced texture upload and allocation-recovery defects:

- Correct mip handling for resized textures and placeholders.
- Consistent RGBA8 storage and conversion for packed 4444/565 texture updates.
- Checked recovery after failed alpha uploads or CPU downsampling.
- Avoids retrying a larger allocation after a smaller one already failed.
- Reports unrecoverable graphics-memory failure instead of continuing with
  resized render targets or failed placeholder storage.

Install `pvz2-vita-latest.vpk` over the existing version. Keep your game files
and `userdata/`; no game data replacement is required.

Six regression cases fail against RC11 and pass against RC12. Compiled ARM
checks verify the storage/update pixel bytes and the prior timezone fix;
shader lifetime and pixel-worker checks also pass. No Vita3K was used.

The new RC11 log contains four texture allocation errors and reaches 189600
frames. Both supplied crash dumps are duplicates of older timezone crashes.
**These fixes are verified in isolated tests, but the reported latest crash
is not conclusively identified and RC12 hardware confirmation is pending.**
Placeholder recovery may omit artwork. If graphics allocation cannot recover,
the app reports an error and must be relaunched. Busy-scene FPS drops remain.

For a recurring crash, retain the newly generated dump and
`ux0:data/pvz2/userdata/loader.log` before relaunching, plus `stall.log` if present.
Include the build ID and the world/level/menu action.

VPK size: 1,137,796 bytes. SHA-256:
```text
071f5d09995b4309878663e7ca9caa39a0489ccbb977af738a9090a64c15ec7e
```
