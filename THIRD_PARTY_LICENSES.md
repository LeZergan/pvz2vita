# Source origins and notices

- Android native Vita loader/runtime groundwork: Andy Nguyen,
  Rinnegatamante, GrapheneCt and Volodymyr Atamanenko. Copyright notices
  remain in the source; the MIT license text is in `vita/direct/LICENSE`.
- FalsoJNI by Volodymyr Atamanenko: MIT;
  `vita/direct/lib/falso_jni/LICENSE` and its README retain the project notice.
- miniz: `vita/direct/third_party/miniz/LICENSE`.
- Pthreads-embedded condition cleanup by Jason Schmidlapp, based on
  Pthreads-win32 by John E. Bossom and contributors: LGPL-2.0-or-later.
  The corrected destructor retains its notices in
  `vita/direct/source/reimpl/pthread_cond_native.c`; license text is in
  `vita/direct/third_party/pthread-embedded/LICENSE`.
- 64-bit time conversion: Michael G Schwern; MIT notice in
  `vita/direct/source/reimpl/time64.c`.
- SHA-1 implementation: Brad Conte; original notice retained in
  `vita/direct/lib/sha1/sha1.c`.
- Exact 4.5.2 runtime/input behavior was informed by
  [OptiJuegos/PvZ2Native](https://github.com/OptiJuegos/PvZ2Native), local baseline
  commit `031e97e5`. The desktop emulator and its vendored dependencies are not
  part of this Vita source snapshot.
- External link dependencies include vitaGL (LGPL-3.0-or-later), vitaShaRK,
  VitaSDK/newlib, pthread, math-neon, OpenSLES, taiHEN and kubridge. They retain
  their own licenses. They are supplied through the developer's SDK; this
  repository does not redistribute the SDK or kernel plugins. The included
  kubridge `.a` files are small import stubs, not the kernel module.

Keep all upstream notices when distributing derived source or binaries.
Original port contributions follow the MIT notices in their files; artwork
and proprietary game data are not relicensed by those notices. The supplied
LiveArea artwork includes game branding belonging to its respective owners.
The September 7 replacement LiveArea package credits assets to standard republic
and the port to KingTorro in its supplied XML. The package keeps that credit text
unchanged; it does not grant a new license over the game's branded artwork.
