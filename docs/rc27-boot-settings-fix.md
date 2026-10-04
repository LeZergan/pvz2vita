# RC27: physical Vita boot settings chord

The user reported RC26 settings did not open while holding Down + Cross + L + R.
The menu used `sceCtrlPeekBufferPositive` with `SCE_CTRL_L1`/`SCE_CTRL_R1`.
The VitaSDK header documents that only `sceCtrlPeekBufferPositiveExt2` maps
physical Vita L/R buttons to these bits; the legacy API uses
`SCE_CTRL_LTRIGGER`/`SCE_CTRL_RTRIGGER` instead. Both menu input reads now use
Ext2, matching the gameplay controller's API. This fixes a confirmed API/mask
mismatch; the resulting physical Vita behavior still requires confirmation.

The regression stub now models actual SDK masks and distinguishes the legacy
and Ext2 APIs. It exercises held launch buttons, a press on the final boot
window sample, an incomplete chord, flag-based opening, staged edits, Done,
cancel, locale defaults and framebuffer bounds. RC26's stub used invented
button values and identical input semantics, so it missed the hardware mismatch.

Five relevant checks pass: port-menu, controller, imports-arm, release-setup,
touch-render. The canonical softfp build passes. VPK CRC, current eboot, title
metadata and LiveArea bytes are verified. No emulator/device run or installation
was performed. RC26's save, language, OBB and optimization behavior is retained.

Install the new VPK over the old version, then hold D-pad Down + Cross + L + R
as the app starts. Release the chord once settings appear. Done applies pending
changes and exits; relaunch to play.
