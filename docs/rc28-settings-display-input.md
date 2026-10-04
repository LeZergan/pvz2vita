# RC28: settings display and language defaults

The user confirmed the RC27 chord opens settings but reported flashing and
language override enabling itself. The previous menu cleared and redrew the
framebuffer currently being scanned by the display, permitting visible partial
frames. RC28 allocates two aligned 2 MiB software buffers before game startup.
It draws into the inactive one, submits it for the next frame, waits with
sceDisplayWaitSetFrameBuf, then swaps buffers. These buffers do not run during
gameplay; exiting settings ends the process as before.

Menu actions remain disabled until three successful, non-intercepted samples
show all buttons released. Partial launch-chord releases, transient shell
interception and a repeated full chord cannot move the selection or enable
override. A release prompt appears while input is blocked. This addresses a
possible launch-edge cause; the exact input sequence from the user's Vita was
not captured.

Language settings now use format version 3. Valid RC26/RC27 version-2 settings
load as Off / English, because accidental and deliberate old overrides cannot
be distinguished. This resets old preferences once; Done writes version 3.
Future deliberate override changes persist normally. Missing/invalid settings
still default to Off with en_US / en / US; Off always requests English.

The production menu test now simulates held, partial, repeated and intercepted
launch input, neutral release, explicit edits and untouched Done. It compares
the actual rendered Off / automatic English rows and verifies the currently
displayed buffer is unchanged while rendering its replacement. Both buffers
alternate and every submission is awaited before reuse. Migration disables all
six old locale overrides; explicit new selections, cancel, recovery and backed
up coin/gem edits remain covered. Five relevant host/ARM checks and the canonical
softfp build pass. VPK CRC, current eboot, SFO and LiveArea bytes are verified.

No device installation or emulator test was performed. Confirmation of visual
stability on physical Vita is pending. Updated VPK is copied to the user's
requested D:/Мой диск/pvz2vita/data/pvz2/ folder with a verified matching hash.
