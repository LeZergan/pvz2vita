# RC17: user-owned frame limit

The default is 30 FPS. Visual Settings still offers 30 or 60 FPS, and an explicit
selection is now saved across app restarts. Nothing automatically changes the
selected FPS target; the adaptive CPU clock policy does not change the target.
Actual measured FPS can still dip when the workload exceeds the budget.

The small preference lives in userdata/fps_preference.txt. A complete previous
copy is retained while replacing it, and startup recovers that copy if a save
was interrupted. Writes occur only on an explicit choice, outside gl_swap.
A failed save leaves the selected target active for the current session and
logs the failure. No game save files are modified. An explicit legacy 30/60
choice in fps_cap.txt is honored only when neither preference copy is valid.

Hold L+R then Down+Cross to open settings. Pause with Start first during play;
the underlying game does not pause automatically when this dialog opens.

Checked default 30, saved 60 across repeated loads/new process, 30/60 changes,
invalid values, partial files, interrupted replacement, and write failure.
Controller/dialog, live frame-budget and power-policy checks also pass.
VPK/SELF/SFO/artwork/index/eboot checks pass. No Vita/Vita3K testing or external
upload/publication. No new claim about measured heat or gameplay FPS.
