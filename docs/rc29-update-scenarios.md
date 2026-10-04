# RC29 additional update scenarios — October 4, 2026

Verdict at audit time: RC29 was ready for a controlled Vita test update. This
audit adds host regressions; no runtime code changed and no replacement binary
was necessary. Publication update: RC29 works on Vita and is published as the
regular 1.1 release. This local audit did not include a complete playthrough or
a new hardware FPS measurement.

## Additional production-menu checks

The test compiles the actual menu, settings and save-editor code with scripted
Vita API input/display stubs. All 13 additional scenarios pass:

| Scenario | Verified result |
| --- | --- |
| Missing save | Settings can be applied; player/currency presses do not create a save. |
| Unsupported/encrypted-format save | Settings can be applied; original save bytes remain unchanged. |
| Edits to two players | Both changes commit together with one exact original backup; unrelated bytes stay identical. |
| Cancel an edit with balances above the nine-digit edit limit | Original high balances remain identical; no backup or save rewrite. |
| Save sync failure representing insufficient storage | Done stays in the menu with the rendered backup error; settings are not committed and the original save remains intact. |
| Edit then restore the original value | Done saves preferences without rewriting the save or making a backup. |
| Cancel after editing several players | All staged currencies and preferences are discarded. |
| Missing OBB | Settings open and show the missing-archive message. This does not establish gameplay without an OBB. |
| Unrecognized OBB header | Settings show the actual size and unknown-header message without crashing. |
| Sixteen players and selection wrapping left | Only the last selected player's coin token changes; all names and other bytes are preserved. |
| Explicit version-3 language override | Deliberately enabled German survives opening settings and Done. |
| Version-3 override Off with an alternate stored locale | The grey language row ignores changes; getters still request English. |
| Interrupted save commit with only the staged original present | Startup restores the original before the menu reads it; no duplicate backup. |

The existing 14 menu regressions also pass, including boot-chord release,
interception, headphones, display failures, legacy defaults, stable alternating
framebuffers and retry after currencies commit but preferences fail.

## Additional save transaction checks

Six added cases pass against copied synthetic saves: staging failure; commit
and rollback failure followed by successful recovery; an external write detected
before commit; temporary-file sync failure; a blocked temporary-file path; and
all 1000 backup slots occupied. Originals or recoverable staged originals remain
available. Occupied backup files and blocked-path contents are unchanged.

These are fault-injection tests of production transaction logic, not a physical
power-loss or Vita filesystem durability test. The preserved real 4.5.2 save
also passes the existing exact-edit test and its source SHA256 stays unchanged.

Evidence: `out/rc29-update-scenarios/checks.json`, `port-menu.log`, and
`save-editor.log`. The RC29 full-suite record remains 60/60; this turn reruns the
two checks that gained new cases. The exported source's corresponding checks
also pass using synthetic data without proprietary game files.

## Exact artifact and remaining device gate

Build: `452-v1.1-rc29`; VPK: 1108934 bytes.

SHA256: `7e7dce42fc69ce08811f5fd8e4627229cedbabc771b8d23c1888fb623f08cc92`.

Latest, Tester and `D:/Мой диск/pvz2vita/data/pvz2/pvz2-vita-latest.vpk` copies
have that same digest. Cloud synchronization is not established.

On Vita, verify this exact build:

1. Open settings repeatedly with Down + Cross + L + R, release the buttons,
   and confirm no flashing or automatic override enabling; include headphones.
2. Cancel an edit, then apply a small coins/gems edit with Done. Relaunch and
   confirm the correct player and values in-game, and normal saving after a level.
3. Load the intended compatible 4.5.2 mod OBB, including its translated English
   assets with override Off. A different engine version remains unsupported.
4. Play a busy zombie wave and check pause/unpause, suspend/resume, audio and input.

No new on-device evidence, edited-save in-game reload, complete playthrough or
hardware FPS measurement was obtained in this audit. The code and package were
locally verified; the device checks above were outstanding at that point.
The current release status is recorded at the top.
