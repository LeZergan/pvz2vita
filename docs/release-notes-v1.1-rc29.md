# Plants vs. Zombies 2 — PS Vita 1.1 RC29

- Added boot settings: hold **Down + Cross (X) + L + R**, then release the buttons. **Done applies changes and exits; relaunch to play.**
- Fixed settings input and display buffering. Language override defaults **Off / English**, with six optional languages.
- Added backed-up coins/gems editing for multiple players, safer writes and interrupted-save recovery.
- Removed stock OBB size/hash restrictions and the bundled stock index. Compatible mod archives use their own resource directory; settings show the loaded OBB info.
- Improved sprite/shader caching and archive indexing. All optimizations are enabled; the target stays 30 FPS.
- Reworked setup, settings, mod and troubleshooting instructions.

Install over the previous VPK; keep game files and back up `userdata/`.
LeZergan confirms RC29 works on Vita. Local checks pass **60/60**, including
**27 menu scenarios**; heavy-wave slowdowns remain possible.

[Setup](../README.md#setup) · [Settings guide](settings.md)

Build: `452-v1.1-rc29` · VPK SHA256:
`7e7dce42fc69ce08811f5fd8e4627229cedbabc771b8d23c1888fb623f08cc92`
