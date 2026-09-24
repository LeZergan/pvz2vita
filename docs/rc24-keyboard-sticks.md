# RC24: restore analog sampling after system dialogs

The tester reports that after changing a nickname, both sticks stop working
while the D-pad still moves the pointer. RC23 repaired pointer ownership but
did not restore the controller sampling modes after the system keyboard.

`controls_restore_sampling()` now sets both standard and extended sampling to
`SCE_CTRL_MODE_ANALOG_WIDE`. Startup uses the same helper. The keyboard calls it
immediately after `sceImeDialogTerm()` on completion/cancel/abort, and on failed
initialization. The controls guide does the equivalent after message-dialog
termination or failed initialization. Nothing reconfigures sampling per frame
or while a system dialog is active.

Tests verify the actual helper restores both simulated digital modes, restores
after termination rather than before it, and runs after 500 alternating text
accept/cancel cycles. Existing empty-text, numeric filtering, initialization
failure, pending-commit and pointer ownership checks remain. The complete 53
local suites are required before packaging.

This addresses a concrete lifecycle gap consistent with the tester symptom.
The actual system sampling change has not been captured on the Vita, and this
build has not been tested there. Fixed 30 FPS, pointer edge clipping, manual
logging opt-in and prior archive improvements remain. The independent blue
initial-menu line is still unresolved.
