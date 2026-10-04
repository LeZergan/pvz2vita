# RC29 release audit — October 3, 2026

Local release candidate `452-v1.1-rc29`; no public release or Vita installation
performed. Physical Vita validation remains required before a final release.

Publication update, October 4: RC29 works on Vita and is published as the regular
1.1 release. The local audit below records the evidence available before
publication; it did not include a complete playthrough or new hardware FPS
measurement.

The [October 4 additional scenario audit](rc29-update-scenarios.md) adds 13
production-menu cases and six save transaction cases. All pass; runtime code
and the RC29 VPK remain unchanged. This extends coverage without replacing the
remaining physical Vita release gate below.

## Fixed in this audit

- Settings release detection now ignores hardware status bits such as connected
  headphones. Intercepted/failed input reads reset the release barrier even after
  the menu has become active, preventing resumed held Cross from changing a row.
- Double-buffer allocation, framebuffer submission and display wait failures
  stop with an error rather than silently loading the game or reusing a buffer
  that may still be displayed. Player names cannot inject layout newlines.
- Done commits all edited players in one lossless, backed-up save transaction.
  A later player's failure cannot leave only the first player's edit committed.
  No-op Done does not rewrite saves or create backups. Unedited fields preserve
  their original numeric encoding, including existing balances above the edit
  limit. Explicit entry uses nine digits and clamps its initial editable value
  to 999999999 when an existing balance is larger.
- Preferences parsing is bounded and rejects integer overflow, trailing data,
  embedded NULs and invalid settings. Synced temporary contents are verified
  before the old file is staged. Failure to clear an old transaction is handled.
  If currencies commit but the separate settings write fails, the screen reports
  that state accurately; retrying Done does not edit or back up the save again.
- Installed OBB parsing bounds directory allocation, total metadata, name/stack
  growth, branch targets, traversal visits and resource ranges. Cyclic or invalid
  repacked directory data cannot hang or publish a partial index. These are
  parser bounds, not stock size/hash checks. The supplied archive still indexes
  all 3710 resource names; compatible repacked names/offsets/lengths are accepted.
- Matrix writes invalidate potentially overlapping sprite-uniform cache values,
  completing the same protection already used by other setters. Optimizations
  remain enabled, and the simulation still runs with the fixed 30 FPS target.
- New-feature checks are included in the exported source CI workflow.
- Sprite metadata uses four entries per bucket while retaining the 128-entry
  limit. Alternating colliding program IDs no longer evicts metadata every call.
  The same compiled collision fixture produces 40000 metadata-query calls and
  20000 driver uploads with the original cache, versus zero of either after
  warming the new cache. This is a call-count comparison, not a Vita FPS result.
- The OBB parser reuses its 96-byte RSG header buffer instead of allocating and
  freeing it for every group (1780 groups in the supplied archive). Two existing
  API const-qualification build warnings are resolved without changing behavior.

## GitHub review

Reviewed [the Vita project](https://github.com/LeZergan/pvz2vita) and
[PvZ2Native](https://github.com/OptiJuegos/PvZ2Native) on October 3, 2026. The Vita
main head is faebc24cfc77f5c4a79a66b28e6d7ff2131b32b3 and still documents RC25.
Its language request is implemented locally; its modern-mod suggestion does not
establish compatibility with this fixed 4.5.2 engine. The upstream main head is
0feee44402a00e9c68da98bcfc403f03f8593043, whose visible change removes decompiled
Java files rather than adding Vita performance code. No remote checkout was
changed and no issue comments or release announcements were posted.

[Pause CPU issue #3](https://github.com/LeZergan/pvz2vita/issues/3) reports RC11
and has no attached runtime log. The local fixed-30-FPS path sleeps instead of
spinning and already has bounded clock policy; its tests pass. The report does
not establish whether RC29 still has the same paused CPU behavior. Busy-wave
native simulation remains serial. Current clocks, shared-core scheduling, fixed
cadence and simulation behavior are retained; hardware profiling is needed to
choose further gameplay optimizations responsibly. GitHub response metadata and
the compiled collision comparison are retained under out/rc29-evidence.

## Preserved behavior

Hold Down + Cross + L + R while booting; release all buttons when settings appear.
Settings display the selected OBB path, bytes/MiB and readable RSB information.
Default language override is Off and requests en_US/en/US. Old version-2
preferences reset to English/Off; explicit version-3 choices persist. A mod that
translates English assets continues to use those assets. The optional six locales
require those language assets in the selected archive.

Done applies changes and exits. Relaunch to play. Circle discards pending edits.
Supported unencrypted 4.5.2 RTON coins/gems are edited; encrypted or unsupported
saves remain unchanged. Native library compatibility checks still protect the
fixed engine addresses. Game/save files are not packaged into the VPK.

## Verification and remaining release gate

All 60 local host/compiled ARM checks pass. The menu regression covers held,
partial and intercepted chords, headphone status, input/display errors, staged
edits, cancel, Done, settings-write retry, automatic English and alternating
framebuffer preservation. Save tests cover atomic multi-profile edits, exact
backups, high unedited balances, malformed saves, I/O faults and recovery.
Preferences faults cover sync, corruption, close, staging, commit and removal.
Mod tests cover unchanged-header/size repacks, changed lengths and malformed
tries. Existing audio/input, memory, threading, imports, graphics and cadence
checks also pass.

Canonical Release build and an independent exported-source Release build pass
without game files. Package CRC, current SELF, SFO title/memory flags, LiveArea,
absence of game data, disabled diagnostics and copied VPK hash are verified.

Before public release, test this exact VPK on Vita: open settings repeatedly
(also with headphones), confirm stable display and Off/English, cancel an edit,
apply a small currency change with Done and reload it in-game, then test a
compatible mod OBB and a busy zombie wave. Confirm suspend/resume, audio/input
and normal saving after a level. No complete playthrough or hardware FPS
measurement is established by host tests. These device checks were pending at
audit time, when the latest published release was RC25. See the publication
update at the top for the current release status.
