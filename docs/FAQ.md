---
description: Why the overlay, the server or your numbers look wrong
---

# FAQ

## The overlay says DISCONNECTED.

The server isn't running, a firewall prompt was declined, or the port differs. The server listens on `127.0.0.1` only. Check host and port in Settings against `--port` and press **Connect / reconnect**. The header shows the last error.

***

## Every value is 0.

No reward is loaded, or your terms fade in over training and the training step is before that. Press **Reload config** and tick **Force late-schedule step** under Setup. If weights are 0 but raw values aren't, your source doesn't fill `weights`, or the term has no weight at that step.

***

## Contact, flip and ball-hit fields stay at defaults.

The arena mirror needs the collision meshes. Start the server from the folder that contains `collision_meshes`, or pass `--meshes <dir>`. The console prints `RocketSim arena mirror enabled` when it found them.

***

## The Shots tab is empty.

The server was started without `--tasks`. Restart it with a task file, or load one under **Task file**.

***

## `load failed`.

The server can't find or parse the config. The path is resolved from the server's working directory, not the game's. Use an absolute path.

***

## CMake says `Target GigaLearnCPP does not exist`.

Add `reward_server` after the line that creates `GigaLearnCPP`, or set `RS_FRAMEWORK_TARGET` to your target's name.

***

## The compiler complains about a missing `GameState` field.

The server reads `RLGC::GameState` and RocketSim. If your fork lacks a field, the error names it. Adapt that line in `reward_eval.cpp` or `rlpr/RLPRExporter.cpp`.

***

## The deploy script says Rocket League is running.

The game keeps the plugin DLL locked. Close it, build with `-NoDeploy`, or use `-Force` and reload the plugin from the F6 console.

***

## The plugin is missing in F2, or fails to load after an update.

If it's missing, the game was running during install or BakkesMod wasn't started. After a game or BakkesMod update, rebuild against the current SDK. A prebuilt DLL from someone else is tied to their SDK.

***

## Clip playback stutters or the game lags.

Open the BakkesMod log and look for the `[replay-diag]` lines. They show ticks per second, the time spent applying each frame, the car counts and how many bots the plugin asked for. Send those lines when reporting it.

***

## Can I trust the contacts in replay output?

Not as game truth. They're reconstructed from the preceding interval by stepping a RocketSim mirror. The JSON marks this with `contact_is_native_rl_truth: false`.

***

## I get a different error.

Run `reward_server.exe --help` to check the option, then read the server console. It prints a line for every load, reset, clip and disconnect. `hello` returns the commands the running server supports ([Protocol](PROTOCOL.md)).
