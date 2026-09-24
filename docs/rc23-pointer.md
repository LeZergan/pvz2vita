# RC23 pointer and keyboard handoff

The controller no longer requires both sticks to return to neutral after touch
or a reset. Held buttons remain suppressed until released, preventing an old
Cross press from becoming a new click. Physical touch still owns input while
a valid finger is present. Eight consecutive missing touch samples release a
stale capture; one missing sample does not fabricate a release.

The cursor stays visible while controller input owns it. A pending/open keyboard
or pending text commit hides it immediately, including the request frame. After
the keyboard closes it can reappear without requiring fresh stick movement.
Dialog release clears seed-tray focus and balances any held pointer/key events.
The outer controls dialog barrier forwards movement with buttons masked, so
holding the keyboard confirm button does not freeze movement or click through.

All four cursor rectangles are intersected with the 960x544 display before
calling vitaGL. The local library at `C:/Users/Max/Tools/vitaGL-src/source/tests.c`
clamps a negative scissor origin without subtracting it from the rectangle
width; keeping cursor rectangles inside bounds avoids that edge distortion.
Framebuffer restoration now precedes restoration of that framebuffer's clip
state. There is no new texture, shader, allocation or sampling worker.

Expanded tests cover:

- Every one of 3,008 edge positions, comparing the full raster to the exact
  clipped 19-pixel cross; no spill into an unrelated row/column.
- Framebuffer, clip, clear-color and color-mask restoration and zero drawing
  calls while hidden.
- 500 touch/stick/held-Cross handoffs with balanced Down/Up and no stray click.
- Keyboard hiding and reappearance, invalid touch-coordinate clamping,
  single versus sustained missing touch samples and reacquisition.
- Existing modal failure, gesture ownership, seed-tray, text commit/cancel,
  fixed cadence, resource and compiled ARM regressions.

The user confirmed the blue right-edge line is always present in the initial
menu, even independently of pointer use. That symptom is not established as
the cursor bug. No crop/black-strip workaround or speculative texture patch
was added. It remains unresolved pending a current screenshot/render evidence.
These are host regression results, not verification on a Vita screen. No Vita
or Vita3K testing or external publication is performed for this build.
