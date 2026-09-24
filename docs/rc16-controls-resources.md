# RC16: controller and resource pass

Private candidate. No console or Vita3K run, install, data copy, or public release.
This is not a measured 60 FPS/temperature guarantee or full gameplay validation.

## Controls

Left stick or D-pad moves a screen pointer; right stick moves it more slowly.
Cross presses at the pointer. Hold Cross while moving to drag, release to drop.
Circle sends Back; Start sends the game's Menu key. Touch remains available;
using touch releases the controller's capture and hides its pointer.

L/R focuses the standard left seed tray. Cross selects the focused position,
then returns the pointer to its previous lawn position. Slots are the eight
standard positions starting at (45,86), spaced 54 pixels at 960x544. This is
coordinate navigation, not introspection of unlocked seeds. Special layouts,
conveyors, shovel, plant food, and world menus remain accessible with the pointer.
L/R never clicks automatically. This adapts the familiar console L/R and Cross
pattern to PvZ2's touch interface; it is not an official PvZ2 controller scheme.
Reference: https://plantsvszombies.wiki.gg/wiki/Plants_vs._Zombies_%28PlayStation_3_and_Xbox_360%29

Hold L + R, then Down + Cross to open Visual Settings. There is one setting:
frame limit, with 60 FPS and Hardcore 30 choices, plus Cancel. It uses the Vita
system dialog. Pause gameplay with Start first: opening settings does not pause
the underlying game. The setting lasts for this app session; it does not write
the memory card. The existing optional fps_cap.txt still supplies startup policy.
Button/touch release barriers prevent the closing input from reaching the game.
Failed dialog initialization returns to normal input after release.

## Resource changes

- A fixed 128-entry JNI tracking pool replaces transient tracking malloc/free
  calls while preserving Java object destruction. It costs 3 KiB on ARM, uses
  the existing lock, and falls back to heap records if all slots are occupied.
  All slots return after concurrent churn, nested destruction and 20,000 live refs.
- Contended JNI reference locks yield 50 microseconds after 64 unsuccessful
  spins. The uncontended path never sleeps. This lets a preempted lock owner run
  instead of continuously consuming a CPU core. The compiled ARM check executes
  this path with an artificially delayed owner.
- Removed motion sensor startup: the game does not consume accelerometer data.
- A single modest present hitch no longer restarts the CPU boost cooldown.
  Expensive CPU work, a severe stall, or three consecutive missed frames still
  restores 444 MHz. Sustained light 30 FPS work can step 444 -> 333 -> 222 MHz,
  with cooldown and headroom thresholds. 60 FPS never selects 222 MHz.
- Changing the frame limit updates the CPU budget immediately. Returning to
  60 uses ordinary vblank pacing; no-vsync remains software capped. The cap is
  on the frame loop, including updates and rendering, not only displayed frames.
- The pointer draws only while used, disappearing after three idle seconds.
  It creates no font/texture/shader/vertex-buffer resources. Its four small
  scissored clears restore framebuffer, scissor, clear color and color mask.
  When hidden it makes no GL calls. Actual GPU timing remains unmeasured.
- INFO input logs are bounded for diagnostic builds. Production already compiled
  those logs out: this is explicitly not a production performance gain.

No new game worker threads, GPU overclock, forced GPU synchronization, resolution
drop, simulation-time modification, automatic resource eviction, or save change.
Retains RC14/15's reduced ETC1 storage, fused parallel conversion, audio STOP
lock-inversion fix, GC synchronization and native draw allocation guard.

## Existing evidence and limits

The supplied loader (4).log is RC13, ending at frame 211200. Its three recorded
2048x2048 GL_OUT_OF_MEMORY uploads are covered by the retained texture work;
there is no fatal/blocked-frame snapshot establishing the Zen Garden freeze.
Its final window reports game=16272us, sampled_draw=1294us, present=2288us,
52.87 FPS, and 15 live JNI refs. Sampled draw time is CPU API time, not GPU
utilization, and the game window includes more than simulation. These figures
cannot justify blindly lowering GPU clocks. GPU/bus clocks remain unchanged.
The console-wide corrupted-file symptom still has no proven cause.

24 host/compiled ARM suites cover new controls and power transitions plus
audio, touch/IME, JNI lifetime/Unicode, texture/storage recovery, shader pairs,
worker affinity/notifications, GC, OOM allocation, time, placement and setup.
They do not establish that every scene, special-mode seed tray, controller
gesture or system-dialog display works on hardware. No game-wide FPS increase
or physical heat reduction is claimed from these checks.

Artifact and exact source/ELF/checks: out/builds/v1.1-rc16.
