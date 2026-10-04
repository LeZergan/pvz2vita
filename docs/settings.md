# Boot settings, languages and save editing

Available in RC29 (`452-v1.1-rc29`). Settings run before game startup.
**Done applies pending changes and exits the app. Relaunch to play.**

## Opening settings

Hold **D-pad Down + Cross (X) + L + R** as the app starts, during the first
1.5 seconds of boot. Keep all four held until the screen appears, then release
them. The screen asks you to release the launch buttons before it accepts input.

Use Up/Down to select a row; Left/Right or Cross changes an available option.
Circle outside number entry exits without applying pending edits.

If the chord is difficult to time, create an empty file at
`ux0:data/pvz2/userdata/port_menu.txt` using VitaShell, then launch. Remove that
file when finished; while it exists, every launch opens settings. Done exits,
so the file must be removed before relaunching into the game.

RC29 uses the extended controller API for physical L/R and two alternating
display buffers. Held launch keys, headphone status and intercepted input do not
enable language override. Stable display on each physical Vita still needs
device confirmation; report recurring flashing with the exact build ID.

## OBB information and mods

The top of the screen shows the selected OBB filename, data folder, file size
and readable RSB version/resource-group count. An unknown header or unreadable
archive is reported as information. Settings do not compare the OBB with stock
sizes or hashes and do not require particular languages or a mod allowlist.

Put a compatible mod archive at `ux0:data/pvz2/game.obb`. The older filename
`main.147.com.ea.game.pvz2_row.obb` also works; `game.obb` wins when both exist.
Close the game before replacing an archive, and back up `userdata/` before
switching mods. The loader rebuilds its resource index from the installed OBB;
changed names, offsets and archive lengths are accepted. Old extracted blocks
are rebuilt when their contents no longer match the installed archive.

The archive must be readable by the fixed 4.5.2 ROW engine. A newer game's OBB
does not turn the supplied 4.5.2 native library into that newer engine.

## Language defaults

**Language override is Off by default.** Off requests English (`en_US`),
regardless of the Vita system language. The Language row stays grey and reads
**English (automatic)**. A mod translating the English assets uses those
translated assets with override Off; there is no separate translation detection
or mandatory language-content check.

Turn override On only when you want an explicit locale. Available choices:

| Menu choice | Requested locale |
| --- | --- |
| English | `en_US` |
| German | `de_DE` |
| Spanish | `es_ES` |
| French | `fr_FR` |
| Italian | `it_IT` |
| Portuguese (Brazil) | `pt_BR` |

The original archive contains all six. A mod may replace or omit them; select
only a locale that it supplies. No Russian locale was found in the original
archive; Russian text provided by a mod's English assets can still use Off.

RC26/RC27 could accidentally enable override from the boot chord. Their old
version-2 preferences reset once to Off/English. RC28/RC29 version-3 preferences
preserve deliberate choices after Done. Missing or invalid preferences use the
default. Preferences live in `userdata/port_settings.txt`.

## Coins and gems

The editor supports unencrypted 4.5.2 `PlayerInfo` saves at
`ux0:data/pvz2/userdata/No_Backup/pp.dat`, with up to 16 players. Select the player
before editing. It changes coins and gems only; it does not unlock plants,
levels or events. With no readable supported save, those rows cannot edit data,
but language settings remain available. Create a player through normal gameplay
first on a new install. Encrypted or unsupported saves are left unchanged.

1. Highlight Coins or Gems and press Cross to open number entry.
2. Left/Right selects a digit; Up/Down changes that digit. The entry range is
   0–999999999. Existing higher balances stay intact unless explicitly edited;
   opening entry displays a capped value, which Circle can cancel.
3. Cross accepts the value into the pending edits. Circle cancels that number
   and returns to the settings rows. Neither action writes the save yet.
4. Edit another currency or player if needed. Done applies all edited players
   together and exits. Circle outside entry discards all pending changes.
5. Relaunch and confirm the values in-game for the selected player.

Only changed currency tokens are replaced; unrelated save bytes are preserved.
Done without currency changes does not rewrite the save or create a backup.

## Backups and storage errors

Before a currency commit, an exact verified original is saved as
`userdata/No_Backup/pp.dat.backup-N`. Each edit uses an unused number. All edited
players share one transaction; a failure does not commit only the first player.

Save data and language preferences are separate files. If currency saving fails,
Done stays in settings and does not apply preferences. If currencies commit but
preferences fail, the screen says **Currencies saved; settings failed**. Correct
the storage problem and retry Done; the already saved currencies are not backed
up or written again. Circle at that point discards remaining pending edits, not
the currencies that already committed.

To restore a backup, close the game, keep a copy of your current `pp.dat`, and
copy the chosen `pp.dat.backup-N` over `pp.dat` in the same `No_Backup` folder.
Keep the backup itself. An interrupted commit may leave `pp.dat.editor-old`;
when `pp.dat` is absent the loader restores that staged original at startup.
Do not delete it if startup reports a recovery failure.

## Performance options

All port optimizations are enabled by default. There is no optimization row,
sprite-upload toggle or FPS selector. The target remains 30 FPS; busy waves can
still slow down. Logging stays Off unless the separate `ux0:data/pvz2/logging/`
directory is manually created. See [setup and logging](../README.md).
