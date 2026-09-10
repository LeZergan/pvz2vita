# Configuration first-use race (RC6)

Confirmed: the previous bridge published `g_cfg_loaded` before reading
`config.kv`. A second engine thread could then observe missing keys while the
first thread was still loading them. Reads, writes and erases also shared the
key/value arrays without synchronization.

`scripts/check-config-store.py` compiles the actual configuration section from
`java.c`. Its file-open gate holds the first reader while a second reader asks
for the same persisted key. The preserved source in
`out/config-race-evidence/java-before.c` fails: `first=42 second=-1`.
The failure is preserved in `out/config-store-4_mofu9z/result.txt`.

RC6 holds one native pthread mutex across loading and table access. Readers copy
values before releasing the lock, then perform JNI allocation/conversion outside
the lock. Writes now report file-open/write/close failures through the existing
boolean JNI return. Missing strings remain distinct from present empty strings.
No configuration defaults or completion flags were added.

The same first-use replay passes with RC6. The test also checks concurrent writes
and erases, complete 255-byte values, reload from committed bytes, missing/empty
values and injected write-open failure with recovery. Results are preserved in
`out/rc6-config-store.log` and `out/config-store-bh6q161g/result.txt`.

This is a confirmed bridge defect, not a confirmed explanation for the user's
infinite loading flower. Persistence still rewrites the file in place: it is not
power-loss atomic. A failed save may leave the new value in memory.

In Vita3K, RC6 loaded Day 4 under disabled CPU optimization, enabled surface sync,
10 ms file-open delays and Cubeb audio. Changing the music slider changed
`MusicVolume` from 85 to 34 in the cloned profile. Exiting and re-entering the
level preserved that setting. A full process relaunch reached the title screen;
the settings UI and file both retained the changed music volume. Defeat and Retry
also returned to a fresh level. Original user and emulator profiles were untouched.
