# Roadmap

What isn't done, what isn't verified, and what I'd build next.

***

## Not Verified Yet

*   **Watch mode.** The plugin samples saved replays through the SDK's replay wrappers (`IsInReplay`, `GetGameEventAsReplay`, `GetViewTarget`). The server side is covered by a scripted test, the in-game side has not been run against a replay.
*   **The redesigned overlay.** It compiles and links. It has not been looked at in the game.
*   **Auto-mark and auto-advance** rely on the goal event timing. The rules are in [Plugin](PLUGIN.md#shots-tab).

***

## Next

1.  **Clip notes and bookmarks.** A key that marks the moment while you play or watch, stored in the clip's sidecar, with a jump to it in replay and a notes box per clip.
2.  **Clip compare.** Replay two clips of one shot, or one clip under two reward configs, and show per-term totals side by side.
3.  **Task list editor** in the overlay instead of editing JSON.
4.  **Inputs while watching.** Replays have none, so input-based terms read zero. Deriving them from the motion of the car would fill most of it.
5.  **Split `RewardTesterPlugin.cpp`.** The overlay already lives in its own file. Clip replay, probe and server sync are still in one.
6.  **Automated tests** for the replay path and the clip codec round trip, without a running server.
7.  **Protocol versioning.** Bump `proto` when a field changes meaning, and make the plugin refuse an incompatible server with a clear message.
8.  **More than one client.** The server serves one connection at a time.
9.  **Persisted alerts and option toggles** in `reward_tester.ini`.
10. **CI.** A syntax-only compile of server and plugin against a pinned framework revision.

***

## Known Limits

*   Contacts, flip state and game events come from a simulator mirror, not from the game.
*   Without collision meshes those fields stay at their defaults.
*   A plugin build is tied to the BakkesMod SDK it was compiled against.
*   The server and the plugin are Windows only.
*   The overlay uses the ImGui 1.75 bundled with the plugin: no tables, no logarithmic slider flag.
