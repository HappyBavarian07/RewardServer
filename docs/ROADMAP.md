# Roadmap

## Still to check

- Record a fresh clip in Watch mode and see if it plays smoothly.
- Try the overlay in-game, especially auto-mark and auto-advance after a goal.

## Next up

- Clip notes and bookmarks in the sidecar.
- Compare two clips, or try two configs on the same clip.
- Edit shot lists in the overlay.
- Save alerts and toggles in `reward_tester.ini`.
- See if we can guess controller inputs from replay footage.

## Code cleanup

- Split playback, the replay probe, and server sync out of `RewardTesterPlugin.cpp`.
- Offline replay and clip read/write tests. CI build check with a pinned framework revision.
- Protocol version checks and multiple clients.

Windows only for now. Bring the matching BakkesMod SDK to build the plugin. RocketSim needs collision meshes to estimate contacts, flips, and game events.
