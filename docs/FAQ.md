---
description: The usual reasons the overlay, the server or your numbers look wrong
---

# FAQ

## The overlay says DISCONNECTED. Why?

The server isn't running, a firewall prompt was declined, or the port differs. The server listens on `127.0.0.1` only. Check the host and port fields in the overlay against `--port`, then press **Connect / Reconnect**. The error line under the title shows the last failure.

***

## Every value is 0. Why?

Either no reward is loaded, or your terms fade in over training and the overlay's Training Step sits before that. Press **Reload Config** and tick **Force late-schedule step**. If the weights are 0 but the raw values aren't, your source doesn't fill `weights`, or the term really has no weight at that step. The raw column is the one to trust.

***

## Contact, flip and ball-hit fields stay at their defaults. Why?

Those fields come from the arena mirror, and the mirror needs the collision meshes. Start the server from the folder that contains `collision_meshes`, or pass `--meshes <dir>`. The server prints `RocketSim arena mirror enabled` when it found them.

***

## The Shot Tasks tab is empty.

The server was started without `--tasks`. Restart it with a task file, or use **Reload Tasks on Server** and give it a path.

***

## The overlay says `load failed`.

The server couldn't find or parse the config. The path is resolved from the server's working directory, not the game's. Give it an absolute path, or start the server from the folder the path is relative to.

***

## The server won't configure: `Target GigaLearnCPP does not exist`.

`reward_server` has to be added after your framework target. Move the `add_subdirectory` line below the one that creates `GigaLearnCPP`, or set `RS_FRAMEWORK_TARGET` if your target has another name.

***

## The compiler complains about a missing `GameState` field.

The server only reads `RLGC::GameState` and RocketSim. If your fork doesn't have a field the code uses, the error names it. Delete or adapt that line in `reward_eval.cpp` (`BuildGameState`, `MirrorArena`) or in `rlpr/RLPRExporter.cpp`.

***

## The deploy script says Rocket League is running.

The game keeps the plugin DLL locked. Close it, or build with `-NoDeploy` and copy the DLL later.

***

## The plugin is missing in F2, or fails to load after an update.

If it's missing, the game was running during the install or BakkesMod wasn't started. If it fails after a game or BakkesMod update, rebuild against the current SDK. A prebuilt DLL from someone else is tied to their SDK version.

***

## Can I trust the contacts in replay output?

No, not as game truth. They're reconstructed from the preceding interval by stepping a RocketSim mirror, and the JSON says so with `contact_is_native_rl_truth: false`. `--self-test-timing` checks the reconstruction against the simulator's native contacts, not against the game.

***

## I get a different error. What now?

Run `reward_server.exe --help` to check the option you passed, then look at the server console. It prints a line for every load, reset, clip and disconnect. The command list the running server supports is in the reply to `hello` ([Protocol](PROTOCOL.md)). If that doesn't help, open an issue with the console output.
