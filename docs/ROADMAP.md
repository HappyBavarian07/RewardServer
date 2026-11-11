# Roadmap

## Not verified

*   Watch mode against real replays. The server side has a scripted test, the in-game side hasn't been checked.
*   The redesigned overlay, in the game.
*   Auto-mark and auto-advance. They depend on goal event timing.
*   Clip playback quality. Playback logs `[replay-diag]` so the cause of stutter can be measured.

## Next

1.  Clip notes and bookmarks, stored in the clip sidecar, with a jump to the bookmark in playback.
2.  Clip compare: two clips of one shot, or one clip under two configs, with totals side by side.
3.  Task list editor in the overlay.
4.  Inputs while watching. Replays have none. Deriving them from the car's motion would fill most terms.
5.  Split `RewardTesterPlugin.cpp`. The overlay has its own file. Clip playback, probe and server sync are still in one.
6.  Tests for the replay path and the clip codec round trip without a running server.
7.  Protocol versioning: bump `proto` when a field changes meaning and refuse an incompatible server.
8.  More than one client at a time.
9.  Persist alerts and option toggles in `reward_tester.ini`.
10. CI with a syntax-only compile against a pinned framework revision.

## Limits

*   Contacts, flip state and game events come from a simulator mirror, not the game.
*   Without collision meshes those fields keep their defaults.
*   A plugin build is tied to the BakkesMod SDK it was compiled against.
*   Server and plugin are Windows only.
*   The overlay uses the bundled ImGui 1.75: no tables, no logarithmic slider flag.
