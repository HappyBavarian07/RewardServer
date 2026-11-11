# Roadmap

What isn't done, what isn't verified, and what I'd build next.

***

## Not Verified Yet

The last restructure (split server, extension hooks, own clip codec, new CMake variables) was checked like this: every source compiles against the framework headers alone, the plugin builds and links, and RewardServer and the host project configure under CMake. The refactored server has **not** been linked, started or driven from the game yet. Run `tests/smoke_client.py` and a short freeplay session before you rely on it.

***

## Next

1.  **Split `RewardTesterPlugin.cpp`.** It's about 1,700 lines. Overlay, clip replay, probe and server sync want their own files. This needs in-game verification, which is why it's still one file.
2.  **Automated tests.** A fixture clip and a trivial reward would let a build check the replay path and the clip codec round trip. A loopback test inside the server binary would remove the need for a running server.
3.  **Protocol versioning.** Bump `proto` when a field changes meaning, and make the plugin refuse an incompatible server with a clear message.
4.  **More than one client.** The server serves one connection at a time. A second client, such as a viewer or a script, blocks until the first disconnects.
5.  **Per-term history in the overlay.** A short sparkline per term makes brief spikes visible without saving a clip.
6.  **Clip bookmarks.** Mark the moment while playing with a key, store it in the clip, jump to it in replay.
7.  **Clip diff.** Replay two clips of one task and show per-term totals side by side, to compare two reward versions.
8.  **Task list editor** in the overlay, instead of editing JSON.
9.  **Evaluation rate.** Make it configurable and warn on screen when the server falls behind the physics tick.
10. **CI.** A syntax-only compile of server and plugin against a pinned framework revision.

***

## Known Limits

*   Contacts, flip state and game events come from a simulator mirror, not from the game.
*   Without collision meshes those fields stay at their defaults.
*   A plugin build is tied to the BakkesMod SDK it was compiled against.
*   The server and the plugin are Windows only.
