# Original Android controller support audit

2026-09-14, read-only inspection of the supplied PvZ2 4.5.2 ROW native library and decompiled Android input classes. No VPK/game-code change, device execution or game boot.

Result: the Android build exposes basic key actions but no usable full gamepad mapping was found. Vita pointer/seed controls are port-provided; they are not an enabled native console-control mode.

The key-event handler begins at `libPVZ2.so+0xcc67b4`. Its translation branches at `0xcc67e0..0xcc696c` map Android key 4 to internal241, 66 to13, 67 to8 and82 to18: Back, Enter, Delete and Menu. Other codes become0. The executable audit ran those actual ARM branches for every input code0..512 and confirmed that exact map. This includes rejection of D-pad19..23, gamepad buttons96..110, Start108 and Select109. Back translation was stopped before unrelated Java service calls; other cases stopped before logging/dispatch. This proves translation behavior, not the behavior of every widget or game state.

`classesdex/com/popcap/SexyAppFramework/AndroidUIEventManager.java` exposes touch, key, long-press, pinch, flick, Back and text-input records. `AndroidSurfaceView.java` wires touch/gesture input. Searches of the decompiled classes found no gamepad/joystick axis processing (`onGenericMotionEvent`, InputDevice controller routes, or gamepad-axis handling). Native input-related strings include the CharToKeyCode diagnostic but no gamepad/joystick identifiers; unrelated AnimationController strings do not establish controller support. Absence of these paths does not prove that every dormant native engine function lacks keyboard behavior, but there is no demonstrated native gameplay scheme to enable with a setting.

Upstream's existing `pvz2native/include/pvz2native/input/input_queue.h` independently documents the same four accepted keycodes. The Vita port currently maps Start to Android Menu82 and Circle to Back4. Sticks, Cross and shoulders are translated by `vita/direct/source/utils/controller.c` into pointer/touch/seed-tray actions.

Evidence: `out/audit-game-input.py` (string and instruction inspection), `out/verify-game-keycodes.py` (bounded original-ARM translation check), `out/controller-native-audit.json`. Native library SHA256: `eb96a61de9c00538b251420eb2674423eda1865b03a276b03e9cbc9d63eeee00`.
